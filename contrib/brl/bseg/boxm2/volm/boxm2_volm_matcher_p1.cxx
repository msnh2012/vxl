// This is brl/bseg/boxm2/volm/boxm2_volm_matcher_p1.cxx
#include "boxm2_volm_matcher_p1.h"
#include <vul/vul_timer.h>
#include <vcl_where_root_dir.h>
#include <vcl_algorithm.h>

#define GBYTE 1073741824

boxm2_volm_matcher_p1::~boxm2_volm_matcher_p1()
{
  delete                  n_cam_;
  delete                  n_obj_;
  delete        layer_size_buff_;
  delete      depth_length_buff_;
  delete []         grd_id_buff_;
  delete []       grd_dist_buff_;
  delete []  grd_id_offset_buff_;
  delete        grd_weight_buff_;
  delete []         sky_id_buff_;
  delete []  sky_id_offset_buff_;
  delete        sky_weight_buff_;
  delete []         obj_id_buff_;
  delete []  obj_id_offset_buff_;
  delete []   obj_min_dist_buff_;
  delete []      obj_order_buff_;
  delete []     obj_weight_buff_;
  delete [] depth_interval_buff_;
}

bool boxm2_volm_matcher_p1::volm_matcher_p1()
{
  n_cam_ = new unsigned;
  *n_cam_ = (unsigned)query_->get_cam_num();
  n_cam_cl_mem_ = new bocl_mem(gpu_->context(), n_cam_, sizeof(unsigned), " n_cam " );
  if (!n_cam_cl_mem_->create_buffer( CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR )) {
    vcl_cerr << "\n ERROR: creating bocl_mem failed for N_CAM" << vcl_endl;
    delete n_cam_cl_mem_;
    return false;
  }
  n_obj_ = new unsigned;
  *n_obj_ = (unsigned)(query_->depth_regions()).size();
  n_obj_cl_mem_ = new bocl_mem(gpu_->context(), n_obj_, sizeof(unsigned), " n_obj " );
  if (!n_obj_cl_mem_->create_buffer( CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR )) {
    vcl_cerr << "\n ERROR: creating bocl_mem failed for N_OBJ" << vcl_endl;
    delete n_cam_cl_mem_;  delete n_obj_cl_mem_;
    return false;
  }
  unsigned nc = *n_cam_;
  unsigned no = *n_obj_;
  // transfer all queries information to 1D array structure
  query_global_mem_ = 0;
  query_local_mem_ = 0;
  vul_timer trans_query_time;
  if (!this->transfer_query()) {
    vcl_cerr << "\n ERROR: transfering query to 1D structure failed." << vcl_endl;
    return false;
  }
  vcl_cout << "\t 4.1.1 Setting up query in pass 1 matcher for GPU ------> \t" << trans_query_time.all()/1000.0 << " seconds." << vcl_endl;
  
  // create queue
  if (!this->create_queue()) {
    vcl_cerr << "\n ERROR: creating pass 1 matcher queue failed." << vcl_endl;
    return false;
  }

  // create depth_interval 
  depth_interval_buff_ = new float[depth_interval_.size()];
  for (unsigned i = 0; i < depth_interval_.size(); i++)
    depth_interval_buff_[i] = depth_interval_[i];

  depth_interval_cl_mem_ = new bocl_mem(gpu_->context(), depth_interval_buff_, sizeof(float)*(depth_interval_.size()), " depth_interval ");
  if(!depth_interval_cl_mem_->create_buffer( CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR )) {
    vcl_cerr << "\n ERROR: creating bocl_mem failed for DEPTH_INTERVAL" << vcl_endl;
    this->clean_query_cl_mem();
    delete depth_interval_cl_mem_;
    return false;
  }
  depth_length_buff_ = new unsigned;
  *depth_length_buff_ = depth_interval_.size();
  depth_length_cl_mem_ = new bocl_mem(gpu_->context(), depth_length_buff_, sizeof(unsigned), " depth_length ");
  if (!depth_length_cl_mem_->create_buffer( CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR )) {
    vcl_cerr << "\n ERROR: creating bocl_mem failed for DEPTH_LENGTH" << vcl_endl;
    this->clean_query_cl_mem();
    delete depth_interval_cl_mem_;    delete depth_length_cl_mem_;
    return false;
  }
  // compile the kernel
  vcl_string identifier = gpu_->device_identifier();
  if (kernels_.find(identifier) == kernels_.end()) {
    vcl_cout << "\t 4.1.2 Comipling kernels for device " << identifier << vcl_endl;
    vcl_vector<bocl_kernel*> ks;
    if (!this->compile_kernel(ks)) {
      vcl_cerr << "\n ERROR: compiling matcher kernel failed." << vcl_endl;
      this->clean_query_cl_mem();
      delete depth_interval_cl_mem_;    delete depth_length_cl_mem_;
      return false;
    }
    kernels_[identifier] = ks;
  }

  // calculate available memory space for indices
  cl_ulong avail_global_mem = device_global_mem_ - query_global_mem_ - sizeof(float)*(depth_interval_.size());
  cl_ulong extra_global_mem = (cl_ulong)(1.5*GBYTE);  // leave extra 1.5 GB space for kernel to run
  if (avail_global_mem < extra_global_mem) {
    vcl_cerr << "\n ERROR: available memory is smaller than pre-defined extra memory, reduce the extra memory space (current value = "
             << extra_global_mem / GBYTE  << ')' << vcl_endl;
    this->clean_query_cl_mem();
    delete depth_interval_cl_mem_;    delete depth_length_cl_mem_;
    return false;
  }
  cl_ulong index_global_mem = avail_global_mem - extra_global_mem;  // in byte

  // note that for each index, we require space for 
  // a float score array with length n_cam
  // a float mean value array with length n_cam*n_obj
  // an uchar index array with length layer_size
  cl_ulong per_index_mem = nc*no*sizeof(float) + nc*sizeof(float) + sizeof(unsigned char)*layer_size_;
  if (index_global_mem < per_index_mem) {
    this->clean_query_cl_mem();
    delete depth_interval_cl_mem_;    delete depth_length_cl_mem_;
    vcl_cerr << "\n ERROR: available memory can not take a single index, reduce the extra memory space (current value = "
             << extra_global_mem / GBYTE  << ')' << vcl_endl; 
    return false;
  }
  unsigned ni = index_global_mem / per_index_mem;

  // create cl_mem for layer_size
  layer_size_buff_ = new unsigned;
  *layer_size_buff_ = layer_size_;
  layer_size_cl_mem_ = new bocl_mem(gpu_->context(), layer_size_buff_, sizeof(unsigned), " layer_size ");
  if (!layer_size_cl_mem_->create_buffer( CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR )) {
    vcl_cerr << "\n ERROR: creating bocl_mem failed for LAYER_SIZE" << vcl_endl;
    this->clean_query_cl_mem();
    delete depth_interval_cl_mem_;        delete depth_length_cl_mem_;
    delete layer_size_cl_mem_;
    return false;
  }

  // hack here for debug purpose
  //ni = 4;
  
  
  vcl_cout << "\t 4.1.3: device have total " << device_global_mem_ << " Byte (" << (float)device_global_mem_/(float)GBYTE << " GB) memory space\n"
           << "\t        query requres " << query_global_mem_ << " Byte (" << (float)query_global_mem_/(float)GBYTE << " GB)\n"
           << "\t        leave " << extra_global_mem  << " Byte (" << (float)extra_global_mem/(float)GBYTE << " GB) extra empty space on device\n"
           << "\t        a single index require " << per_index_mem << " Byte given " << nc << " cameras and " << no << " objects\n"
           << "\t        ---> kernel can calcualte " << ni << " indices per lunching " << vcl_endl;

  // define the work group size and NDRange dimenstion
  work_dim_ = 2;
  local_threads_[0] = 8;
  local_threads_[1] = 8;
  // note the global work goupe size is defined inside the loop since we may have different number of
  // indices passed into buffer

  // -----------------------------------------------------------
  // start loop over all indices
  unsigned leaf_id = 0;
  unsigned round_cnt = 0;
  float gpu_matcher_time = 0.0f;
  vul_timer total_matcher_time;
  cl_int status;
  cl_ulong total_index_num = 0;
  while (leaf_id < leaves_.size()) {
    unsigned char* index_buff_ = new unsigned char[ni*layer_size_];
    // fill the index buffer
    vcl_vector<unsigned> l_id;  // leaf_id for indices filled into buffer
    vcl_vector<unsigned> h_id;  // hypo_id in this leaf_id for indices filled into buffer
    unsigned actual_n_ind = ni; // handling the last round where the number of loaded indices is smaller than pre-computed ni
    if(!this->fill_index(ni, layer_size_, leaf_id, index_buff_, l_id, h_id, actual_n_ind) ) {
      vcl_cerr << "\n ERROR: passing index into index buffer failed for " << leaf_id << " leaf" << vcl_endl;
      this->clean_query_cl_mem();
      delete depth_interval_cl_mem_;        delete depth_length_cl_mem_;
      delete layer_size_cl_mem_;
      delete [] index_buff_;
      return false;
    }
    // resize the index buffer if actual loaded index size is smaller than pre-defined
    if (actual_n_ind != ni)
      ni = actual_n_ind;
    
    total_index_num += ni;
    float* score_buff_ = new float[ni*nc];
    float*    mu_buff_ = new float[ni*no*nc];
    unsigned* n_ind_ = new unsigned;
    *n_ind_ = ni;
    bocl_mem* n_ind_cl_mem_ = new bocl_mem(gpu_->context(), n_ind_, sizeof(unsigned), " n_ind ");
    if (!n_ind_cl_mem_->create_buffer( CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR )) {
      vcl_cerr << "\n ERROR: creating bocl_mem failed for N_IND" << vcl_endl;
      this->clean_query_cl_mem();
      delete depth_interval_cl_mem_;        delete depth_length_cl_mem_;
      delete layer_size_cl_mem_;
      delete n_ind_cl_mem_;     delete [] n_ind_;
      delete [] index_buff_;    delete [] score_buff_;    delete [] mu_buff_;
      return false;
    }

    // define work group size based on number of cameras, nc,  and number of indices, ni;
    cl_ulong cl_ni = (cl_ulong)RoundUp(*n_ind_, (int)local_threads_[0]);   // row is index
    cl_ulong cl_nj = (cl_ulong)RoundUp(*n_cam_, (int)local_threads_[1]);   // col is camera
    global_threads_[0] = cl_ni;
    global_threads_[1] = cl_nj;

    vcl_cout << " --------  in round " << round_cnt++ << " ------ " << vcl_endl;
    vcl_cout << " Giving " << nc << " camera hypos per location and " << ni << " locations pre lunching\n";
    vcl_cout << " NDRange stucture:\n";
    vcl_cout << " \t dimension = " << work_dim_ << vcl_endl;
    vcl_cout << " \t work group size = (" << local_threads_[0] << ", " << local_threads_[1] << ")\n";
    vcl_cout << " \t number of work item =  (" << global_threads_[0] << ", " << global_threads_[1] << ")\n";
    vcl_cout << " \t number of work group = (" << global_threads_[0]/local_threads_[0]
             << ", " << global_threads_[1]/local_threads_[1] << ')' << vcl_endl;

#if 0
    // check loaded indices and associated ids
    vcl_cout << " -------> leaf_id_updated = " << leaf_id << vcl_endl;
    for(unsigned i = 0; i < ni; i++) {
      vcl_cout << " i = " << i << ", leaf_id = " << l_id[i] << " hypo_id = " << h_id[i] << "\n\t";
      unsigned start = i*layer_size_;
      unsigned end = (i+1)*layer_size_;
      for (unsigned j = start; j < end; j++)
        vcl_cout << " " << (int)index_buff_[j];
      vcl_cout << '\n';
    }
#endif

    // create cl_mem_ for index, score and mu
    // Note: here the data passed into device may be smaller than the pre-assigned index_buff size (since actual_n_ind < pre-calculated ni)
    bocl_mem* index_cl_mem_ = new bocl_mem(gpu_->context(), index_buff_, sizeof(unsigned char)*ni*layer_size_, " index ");
    if (!index_cl_mem_->create_buffer( CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR )) {
      vcl_cerr << "\n ERROR: creating bocl_mem failed for INDEX" << vcl_endl;
      this->clean_query_cl_mem();
      delete depth_interval_cl_mem_;        delete depth_length_cl_mem_;
      delete layer_size_cl_mem_;
      delete n_ind_cl_mem_;     delete [] n_ind_;
      delete index_cl_mem_;
      delete [] index_buff_;    delete [] score_buff_;    delete [] mu_buff_;
      return false;
    }
    bocl_mem* score_cl_mem_ = new bocl_mem(gpu_->context(), score_buff_, sizeof(float)*ni*nc, " score ");
    if (!score_cl_mem_->create_buffer( CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR )) {
      vcl_cerr << "\n ERROR: creating bocl_mem failed for SCORE" << vcl_endl;
      this->clean_query_cl_mem();
      delete depth_interval_cl_mem_;        delete depth_length_cl_mem_;
      delete layer_size_cl_mem_;
      delete n_ind_cl_mem_;
      delete index_cl_mem_;     delete score_cl_mem_;
      delete [] index_buff_;    delete [] score_buff_;    delete [] mu_buff_;
      return false;
    }
    bocl_mem* mu_cl_mem_ = new bocl_mem(gpu_->context(), mu_buff_, sizeof(float)*ni*nc*no, " mu ");
    if (!mu_cl_mem_->create_buffer( CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR )) {
      vcl_cerr << "\n ERROR: creating bocl_mem failed for MU" << vcl_endl;
      this->clean_query_cl_mem();
      delete depth_interval_cl_mem_;        delete depth_length_cl_mem_;
      delete layer_size_cl_mem_;
      delete n_ind_cl_mem_;     delete [] n_ind_;
      delete index_cl_mem_;     delete score_cl_mem_;     delete mu_cl_mem_;
      delete [] index_buff_;    delete [] score_buff_;    delete [] mu_buff_;
      return false;
    }

    // start the obj_based kernel matcher
    vcl_string identifier = gpu_->device_identifier();
    if (!this->execute_matcher_kernel(gpu_, queue_, kernels_[identifier][0], n_ind_cl_mem_, index_cl_mem_, score_cl_mem_, mu_cl_mem_)) {
      vcl_cerr << "\n ERROR: executing pass 1 kernel failed on device " << identifier << vcl_endl;
      this->clean_query_cl_mem();
      delete depth_interval_cl_mem_;        delete depth_length_cl_mem_;
      delete layer_size_cl_mem_;
      delete n_ind_cl_mem_;     delete [] n_ind_;
      delete index_cl_mem_;     delete score_cl_mem_;     delete mu_cl_mem_;
      delete [] index_buff_;    delete [] score_buff_;    delete [] mu_buff_;
      return false;
    }
    // block everything to ensure the reading score
    status = clFinish(queue_);
    // read score
    score_cl_mem_->read_to_buffer(queue_);
    mu_cl_mem_->read_to_buffer(queue_);
    status = clFinish(queue_);
    // count time
    gpu_matcher_time += kernels_[identifier][0]->exec_time();

    // post-processing data
    for(unsigned ind_id = 0; ind_id < ni; ind_id++) {
      boxm2_volm_score_out score_out;
      // find maximum for index ind_id
      float max_score = 0.0f;
      unsigned max_cam_id = 0;
      vcl_vector<unsigned> cam_vec;
      for (unsigned cam_id = 0; cam_id < nc; cam_id++) {
        unsigned id = cam_id + ind_id*nc;
        float current_score = score_buff_[id];
        if (max_score < current_score) {
          max_score = current_score;
          max_cam_id = cam_id;
        }
        if (current_score > cam_thres_)
          cam_vec.push_back(cam_id);
      }
      score_all_.push_back(boxm2_volm_score_out(l_id[ind_id], h_id[ind_id], max_score, max_cam_id, cam_vec));
    }
    // clean cl_mem before next round matcher
    delete n_ind_cl_mem_;
    status = clFinish(queue_);
    check_val(status, MEM_FAILURE, " release N_INDEX failed on device " + gpu_->device_identifier() + error_to_string(status));
    delete index_cl_mem_;
    status = clFinish(queue_);
    check_val(status, MEM_FAILURE, " release INDEX failed on device " + gpu_->device_identifier() + error_to_string(status));
    delete score_cl_mem_;
    status = clFinish(queue_);
    check_val(status, MEM_FAILURE, " release SCORE failed on device " + gpu_->device_identifier() + error_to_string(status));
    delete mu_cl_mem_;
    status = clFinish(queue_);
    check_val(status, MEM_FAILURE, " release MU(average depth value) failed on device " + gpu_->device_identifier() + error_to_string(status));

   
    // do the test to ensure kernel is correct
    this->volm_matcher_p1_test(ni, index_buff_, score_buff_, mu_buff_);

    // finish current round, clean host memory
    delete n_ind_;
    delete [] index_buff_;    delete [] score_buff_;    delete [] mu_buff_;

  } // end of loop over all leaves
  
  // time
  float total_time = total_matcher_time.all();
  vcl_cout << "\t\t total time for " << total_index_num << " indices and " << *n_cam_ << " cameras -------> " << total_time/1000.0 << " seconds (" << total_time << " ms)\n" ;
  vcl_cout << "\t\t GPU kernel execution ------------------> " << gpu_matcher_time/1000.0 << " seconds (" << gpu_matcher_time << " ms)\n";
  vcl_cout << "\t\t CPU host execution --------------------> " << (total_time - gpu_matcher_time)/1000.0 << " seconds (" << total_time - gpu_matcher_time << " ms)" << vcl_endl;

  // clear query_cl_mem
  this->clean_query_cl_mem();
  return true;
}

bool boxm2_volm_matcher_p1::fill_index(unsigned const& n_ind,
                                       unsigned const& layer_size,
                                       unsigned& leaf_id,
                                       unsigned char* index_buff,
                                       vcl_vector<unsigned>& l_id,
                                       vcl_vector<unsigned>& h_id,
                                       unsigned& actual_n_ind)
{
  if (is_last_pass_) {
    vcl_cerr << " pass 1 check whether we have last_pass is NOT implemented yet ... " << vcl_endl;
    return false;
  } else {
    unsigned li;
    unsigned cnt = 0;
    for (li = leaf_id; li < leaves_.size(); li++) {
      if (!leaves_[li]->hyps_)
        continue;
      vgl_point_3d<double> h_pt;
      unsigned h_cnt = 0;
      while (cnt < n_ind && leaves_[li]->hyps_->get_next(0,1,h_pt) ) {
        if(is_candidate_) {
          if (cand_poly_.contains(h_pt.x(), h_pt.y())) {  // having candiate list and current hypo is inside it --> accept
            unsigned char* values = index_buff + cnt * layer_size;
            ind_vec_[li]->get_next(values, layer_size);
            cnt++;
            l_id.push_back(li);  h_id.push_back(leaves_[li]->hyps_->current_-1);
          } else {                                       // having candidate list but current hypo is outside candidate list --> ignore
            vcl_vector<unsigned char> values(layer_size);
            ind_vec_[li]->get_next(values);
          }
        } else {                                         // no candidate list, put all indices into buffer
          unsigned char* values = index_buff + cnt * layer_size;
          ind_vec_[li]->get_next(values, layer_size);
          cnt++;
          l_id.push_back(li);  h_id.push_back(leaves_[li]->hyps_->current_-1);
        }
      } // end of while loop for hypos
      if (cnt == n_ind) {
          leaf_id = li; 
          break;
      }
    } // end of for loop for leaves
    if (li == leaves_.size())
      leaf_id = li;              // ensures that we can finish the while loop outside ... (ln 128)
    if (cnt != n_ind) 
      actual_n_ind = cnt;
    return true;
  }
}

// execute kernel
bool boxm2_volm_matcher_p1::execute_matcher_kernel(bocl_device_sptr             device,
                                                   cl_command_queue&             queue,
                                                   bocl_kernel*                   kern,
                                                   bocl_mem*             n_ind_cl_mem_,
                                                   bocl_mem*             index_cl_mem_,
                                                   bocl_mem*             score_cl_mem_,
                                                   bocl_mem*                mu_cl_mem_)
{
  // create a debug buffer
  cl_int status;
  unsigned debug_size = 2000;
  float* debug_buff_ = new float[debug_size];
  vcl_fill(debug_buff_, debug_buff_+debug_size, (float)12.31);
  bocl_mem* debug_cl_mem_ = new bocl_mem(gpu_->context(), debug_buff_, sizeof(float)*debug_size, " debug ");
  if (!debug_cl_mem_->create_buffer( CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR )) {
    vcl_cerr << "\n ERROR: creating bocl_mem failed for DEBUG" << vcl_endl;
    delete debug_cl_mem_;     delete [] debug_buff_;
    return false;
  }
  // set up argument list
  kern->set_arg(n_cam_cl_mem_);
  kern->set_arg(n_obj_cl_mem_);
  kern->set_arg(grd_id_cl_mem_);
  kern->set_arg(grd_id_offset_cl_mem_);
  kern->set_arg(grd_dist_cl_mem_);
  kern->set_arg(grd_weight_cl_mem_);
  kern->set_arg(sky_id_cl_mem_);
  kern->set_arg(sky_id_offset_cl_mem_);
  kern->set_arg(sky_weight_cl_mem_);
  kern->set_arg(obj_id_cl_mem_);
  kern->set_arg(obj_id_offset_cl_mem_);
  kern->set_arg(obj_min_dist_cl_mem_);
  kern->set_arg(obj_weight_cl_mem_);
  kern->set_arg(n_ind_cl_mem_);
  kern->set_arg(layer_size_cl_mem_);
  kern->set_arg(index_cl_mem_);
  kern->set_arg(score_cl_mem_);
  kern->set_arg(mu_cl_mem_);
  kern->set_arg(depth_interval_cl_mem_);
  kern->set_arg(depth_length_cl_mem_);
  kern->set_arg(debug_cl_mem_);
  // set up local memory argument
  kern->set_local_arg((*n_obj_)*sizeof(unsigned char));      // local memory for obj_min_dist
  kern->set_local_arg((*n_obj_)*sizeof(float));               // local memory for obj_weight
  kern->set_local_arg((*depth_length_buff_)*sizeof(float));   // local memory for depth_interval table

  // execute kernel
  if (!kern->execute(queue, work_dim_, local_threads_, global_threads_)) {
    vcl_cerr << "\n ERROR: kernel execuation failed" << vcl_endl;
    delete debug_cl_mem_;
    delete debug_buff_;
    return false;
  }
  status = clFinish(queue);  // block to ensure kernel finishes
  if (status != 0) {
    vcl_cerr << "\n ERROR: " << status << "  kernel matcher failed: " + error_to_string(status) << '\n';
    return false;
  }
  // clear bocl_kernel argument list
  kern->clear_args();
#if 1
  // read debug data from device
  debug_cl_mem_->read_to_buffer(queue);
  unsigned i = 0;
  while( (debug_buff_[i]-12.31)*(debug_buff_[i]-12.31)>0.0001 && i < 1000){
    vcl_cout << " debug[" << i << "] = " << debug_buff_[i] << vcl_endl;
    i++;
  }
#endif
  // clear debug buffer
  delete debug_cl_mem_;
  status = clFinish(queue);
  check_val(status, MEM_FAILURE, " release DEBUG failed on device " + device->device_identifier() + error_to_string(status));
  
  delete [] debug_buff_;
  return true;
}

// clare all query cl_mem pointer
bool boxm2_volm_matcher_p1::clean_query_cl_mem()
{
  bool is_grd_reg = query_->depth_scene()->ground_plane().size();
  bool is_sky_reg = query_->depth_scene()->sky().size();
  bool is_obj_reg = query_->depth_regions().size();
  delete n_cam_cl_mem_;
  delete n_obj_cl_mem_;
  if ( is_grd_reg ) { delete grd_id_cl_mem_;  delete grd_dist_cl_mem_;       delete grd_id_offset_cl_mem_;  delete grd_weight_cl_mem_; }
  if ( is_sky_reg ) { delete sky_id_cl_mem_;  delete sky_id_offset_cl_mem_;  delete sky_weight_cl_mem_; }
  if ( is_obj_reg ) { delete obj_id_cl_mem_;  delete obj_id_offset_cl_mem_;  delete obj_weight_cl_mem_; delete obj_min_dist_cl_mem_;  delete obj_order_cl_mem_; }
  return true;
}

// compile kernel
bool boxm2_volm_matcher_p1::compile_kernel(vcl_vector<bocl_kernel*>& vec_kernels)
{
  // declare the kernel source
  vcl_vector<vcl_string> src_paths;
  vcl_string volm_cl_source_dir = vcl_string(VCL_SOURCE_ROOT_DIR) + "/contrib/brl/bseg/boxm2/volm/cl/";
  src_paths.push_back(volm_cl_source_dir + "generalized_volm_obj_based_matching.cl");
  // create the obj_based matching kernel
  bocl_kernel* kern_matcher = new bocl_kernel();
  if (!kern_matcher->create_kernel(&gpu_->context(), gpu_->device_id(), src_paths,
                                   "generalized_volm_obj_based_matching",
                                   "",
                                   "generalized_volm_obj_based_matching") )
    return false;
  vec_kernels.push_back(kern_matcher);
  
  return true;
}

bool boxm2_volm_matcher_p1::create_queue()
{
  cl_int status = SDK_FAILURE;
  queue_ = clCreateCommandQueue(gpu_->context(),
                               *(gpu_->device_id()),
                               CL_QUEUE_PROFILING_ENABLE,
                               &status);
  if ( !check_val(status, CL_SUCCESS, error_to_string(status)) )
    return false;
  return true;
}

bool boxm2_volm_matcher_p1::write_matcher_result()
{
  return true;
}


bool boxm2_volm_matcher_p1::transfer_query()
{
  bool is_grd_reg = query_->depth_scene()->ground_plane().size();
  bool is_sky_reg = query_->depth_scene()->sky().size();
  bool is_obj_reg = query_->depth_regions().size();

  query_global_mem_ = 3 * sizeof(unsigned);   // n_cam + n_obj + n_ind
   query_local_mem_ = 3 * sizeof(unsigned);   // n_cam + n_obj + n_ind

  // construct the ground_id, ground_dist, ground_offset 1D array
  if (is_grd_reg) {
    unsigned grd_vox_size = query_->get_ground_id_size();
    grd_id_buff_ = new unsigned[grd_vox_size];
    grd_dist_buff_ = new unsigned char[grd_vox_size];
    grd_id_offset_buff_ = new unsigned[*n_cam_+1];
    unsigned grd_count = 0;
    vcl_vector<vcl_vector<unsigned> >& grd_id = query_->ground_id();
    vcl_vector<vcl_vector<unsigned char> >& grd_dist = query_->ground_dist();
    for (unsigned cam_id = 0; cam_id < *n_cam_; cam_id++) {
      grd_id_offset_buff_[cam_id] = grd_count;
      for (unsigned vox_id = 0; vox_id < grd_id[cam_id].size(); vox_id++) {
        unsigned i = grd_count + vox_id;
        grd_id_buff_[i] = grd_id[cam_id][vox_id];
        grd_dist_buff_[i] = grd_dist[cam_id][vox_id];
      }
      grd_count += (unsigned)grd_id[cam_id].size();
    }
    // construct weight parameters
    grd_id_offset_buff_[*n_cam_] = grd_count;
    grd_weight_buff_ = new float;
    *grd_weight_buff_ = query_->grd_weight();
    grd_id_cl_mem_ = new bocl_mem(gpu_->context(), grd_id_buff_, sizeof(unsigned)*grd_vox_size, " grd_id " );
    if (!grd_id_cl_mem_->create_buffer( CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR )) {
      vcl_cerr << "\n ERROR: creating bocl_mem failed for GROUND_ID" << vcl_endl;
      delete n_cam_cl_mem_;   delete n_obj_cl_mem_;
      delete grd_id_cl_mem_;
      return false;
    }
    grd_dist_cl_mem_ = new bocl_mem(gpu_->context(), grd_dist_buff_, sizeof(unsigned char)*grd_vox_size, " grd_dist " );
    if (!grd_dist_cl_mem_->create_buffer( CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR )) {
      vcl_cerr << "\n ERROR: creating bocl_mem failed for GROUND_DIST" << vcl_endl;
      delete n_cam_cl_mem_;   delete n_obj_cl_mem_;
      delete grd_id_cl_mem_;  delete grd_dist_cl_mem_;
      return false;
    }
    grd_id_offset_cl_mem_ = new bocl_mem(gpu_->context(), grd_id_offset_buff_, sizeof(unsigned)*(*n_cam_+1), " grd_offset " );
    if (!grd_id_offset_cl_mem_->create_buffer( CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR )) {
      vcl_cerr << "\n ERROR: creating bocl_mem failed for GROUND_OFFSET" << vcl_endl;
      delete n_cam_cl_mem_;   delete n_obj_cl_mem_;
      delete grd_id_cl_mem_;  delete grd_dist_cl_mem_;  delete grd_id_offset_cl_mem_;
      return false;
    }
    grd_weight_cl_mem_ = new bocl_mem(gpu_->context(), grd_weight_buff_, sizeof(float), " grd_weight " );
    if (!grd_weight_cl_mem_->create_buffer( CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR )) {
      vcl_cerr << "\n ERROR: creating bocl_mem failed for GROUND_WEIGHT" << vcl_endl;
      delete n_cam_cl_mem_;   delete n_obj_cl_mem_;
      delete grd_id_cl_mem_;  delete grd_dist_cl_mem_;  delete grd_id_offset_cl_mem_;  delete grd_weight_cl_mem_;
      return false;
    }
    query_global_mem_ += (sizeof(unsigned char)+sizeof(unsigned))*grd_vox_size;  // ground id and dist
    query_global_mem_ += sizeof(unsigned)*(query_->ground_offset().size());      // ground offset array
    query_global_mem_ += sizeof(float);                                          // ground weight
  }
  
  // construct sky_id buff
  if (is_sky_reg) {
    unsigned sky_vox_size = query_->get_sky_id_size();
    sky_id_buff_ = new unsigned[sky_vox_size];
    sky_id_offset_buff_ = new unsigned[*n_cam_+1];
    unsigned sky_count = 0;
    vcl_vector<vcl_vector<unsigned> >& sky_id = query_->sky_id();
    for (unsigned cam_id = 0; cam_id < *n_cam_; cam_id++) {
      sky_id_offset_buff_[cam_id] = sky_count;
      for (unsigned vox_id = 0; vox_id < sky_id[cam_id].size(); vox_id++) {
        unsigned i = sky_count + vox_id;
        sky_id_buff_[i] = sky_id[cam_id][vox_id];
      }
      sky_count += (unsigned)sky_id[cam_id].size();
    }
    sky_id_offset_buff_[*n_cam_] = sky_count;
    // construct weight parameters
    sky_weight_buff_ = new float;
    *sky_weight_buff_ = query_->sky_weight();
    sky_id_cl_mem_ = new bocl_mem(gpu_->context(), sky_id_buff_, sizeof(unsigned)*sky_vox_size, " sky_id " );
    if(!sky_id_cl_mem_->create_buffer( CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR )) {
      vcl_cerr << "\n ERROR: creating bocl_mem failed for SKY_ID" << vcl_endl;
      delete n_cam_cl_mem_;   delete n_obj_cl_mem_;
      if(is_grd_reg) { delete grd_id_cl_mem_;  delete grd_dist_cl_mem_;  delete grd_id_offset_cl_mem_;  delete grd_weight_cl_mem_; }
      delete sky_id_cl_mem_;
      return false;
    }
    sky_id_offset_cl_mem_ = new bocl_mem(gpu_->context(), sky_id_offset_buff_, sizeof(unsigned)*(*n_cam_+1), " sky_offset " );
    if(!sky_id_offset_cl_mem_->create_buffer( CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR )) {
      vcl_cerr << "\n ERROR: creating bocl_mem failed for SKY_OFFSET" << vcl_endl;
      delete n_cam_cl_mem_;   delete n_obj_cl_mem_;
      if(is_grd_reg) { delete grd_id_cl_mem_;  delete grd_dist_cl_mem_;  delete grd_id_offset_cl_mem_;  delete grd_weight_cl_mem_; }
      delete sky_id_cl_mem_;  delete sky_id_offset_cl_mem_;
      return false;
    }
    sky_weight_cl_mem_ = new bocl_mem(gpu_->context(), sky_weight_buff_, sizeof(float), " sky_weight " );
    if(!sky_weight_cl_mem_->create_buffer( CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR )) {
      vcl_cerr << "\n ERROR: creating bocl_mem failed for SKY_WEIGHT" << vcl_endl;
      delete n_cam_cl_mem_;   delete n_obj_cl_mem_;
      if(is_grd_reg) { delete grd_id_cl_mem_;  delete grd_dist_cl_mem_;  delete grd_id_offset_cl_mem_;  delete grd_weight_cl_mem_; }
      delete sky_id_cl_mem_;  delete sky_id_offset_cl_mem_;  delete sky_weight_cl_mem_;
      return false;
    }
    query_global_mem_ += sizeof(unsigned)*sky_vox_size;                          // sky id
    query_global_mem_ += sizeof(unsigned)*query_->sky_offset().size();           // sky dist
    query_global_mem_ += sizeof(float);                                          // sky weight
  }
  
  // construct obj_id, obj_offset 1D array
  if (is_obj_reg) {
    unsigned obj_vox_size = query_->get_dist_id_size();
    unsigned obj_offset_size = (*n_cam_) * (*n_obj_);
    obj_id_buff_ = new unsigned[obj_vox_size];
    obj_id_offset_buff_ = new unsigned[obj_offset_size+1];
    vcl_vector<vcl_vector<vcl_vector<unsigned> > >& dist_id = query_->dist_id(); 
    unsigned obj_count = 0;
    for (unsigned cam_id = 0; cam_id < *n_cam_; cam_id++) {
      for (unsigned obj_id = 0; obj_id < *n_obj_; obj_id++) {
        unsigned offset_id = obj_id + cam_id * (*n_obj_);
        obj_id_offset_buff_[offset_id] = obj_count;
        for (unsigned vox_id = 0; vox_id < dist_id[cam_id][obj_id].size(); vox_id++) {
          unsigned i = obj_count + vox_id;
          obj_id_buff_[i] = dist_id[cam_id][obj_id][vox_id];
        }
        obj_count += (unsigned)dist_id[cam_id][obj_id].size();
      }
    }
    obj_id_offset_buff_[obj_offset_size] = obj_count;
    // construct min_dist, max_dist and order_obj for non-ground, non-sky object
    obj_min_dist_buff_ = new unsigned char[*n_obj_];
    //max_obj_dist_buff = new unsigned char[n_obj_];
    obj_order_buff_ = new unsigned char[*n_obj_];
    for (unsigned obj_id = 0; obj_id < *n_obj_; obj_id++) {
      obj_min_dist_buff_[obj_id] = query_->min_obj_dist()[obj_id];
      obj_order_buff_[obj_id] = query_->order_obj()[obj_id];
    }
    obj_weight_buff_ = new float[*n_obj_];
    for (unsigned obj_id = 0; obj_id < *n_obj_; obj_id++)
      obj_weight_buff_[obj_id] = query_->obj_weight()[obj_id];
    // create corresponding cl_mem
    obj_id_cl_mem_ = new bocl_mem(gpu_->context(), obj_id_buff_, sizeof(unsigned)*obj_vox_size, " obj_id " );
    if(!obj_id_cl_mem_->create_buffer( CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR )) {
      vcl_cerr << "\n ERROR: creating bocl_mem failed for OBJ_ID" << vcl_endl;
      delete n_cam_cl_mem_;   delete n_obj_cl_mem_;
      if(is_grd_reg) { delete grd_id_cl_mem_;  delete grd_dist_cl_mem_;  delete grd_id_offset_cl_mem_;  delete grd_weight_cl_mem_; }
      if(is_obj_reg) { delete sky_id_cl_mem_;  delete sky_id_offset_cl_mem_;  delete sky_weight_cl_mem_; }
      delete obj_id_cl_mem_;
      return false;
    }
    obj_id_offset_cl_mem_ = new bocl_mem(gpu_->context(), obj_id_offset_buff_, sizeof(unsigned)*(obj_offset_size+1), " obj_offset " );
    if(!obj_id_offset_cl_mem_->create_buffer( CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR )) {
      vcl_cerr << "\n ERROR: creating bocl_mem failed for OBJ_OFFSET" << vcl_endl;
      delete n_cam_cl_mem_;   delete n_obj_cl_mem_;
      if(is_grd_reg) { delete grd_id_cl_mem_;  delete grd_dist_cl_mem_;  delete grd_id_offset_cl_mem_;  delete grd_weight_cl_mem_; }
      if(is_obj_reg) { delete sky_id_cl_mem_;  delete sky_id_offset_cl_mem_;  delete sky_weight_cl_mem_; }
      delete obj_id_cl_mem_;  delete obj_id_offset_cl_mem_;
      return false;
    }
    obj_weight_cl_mem_ = new bocl_mem(gpu_->context(), obj_weight_buff_, sizeof(float)*(*n_obj_), " obj_weight " );
    if(!obj_weight_cl_mem_->create_buffer( CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR )) {
      vcl_cerr << "\n ERROR: creating bocl_mem failed for OBJ_WEIGHT" << vcl_endl;
      delete n_cam_cl_mem_;   delete n_obj_cl_mem_;
      if(is_grd_reg) { delete grd_id_cl_mem_;  delete grd_dist_cl_mem_;  delete grd_id_offset_cl_mem_;  delete grd_weight_cl_mem_; }
      if(is_obj_reg) { delete sky_id_cl_mem_;  delete sky_id_offset_cl_mem_;  delete sky_weight_cl_mem_; }
      delete obj_id_cl_mem_;  delete obj_id_offset_cl_mem_;  delete obj_weight_cl_mem_;
      return false;
    }
    obj_min_dist_cl_mem_ = new bocl_mem(gpu_->context(), obj_min_dist_buff_, sizeof(unsigned char)*(*n_obj_), " obj_min_dist " );
    if(!obj_min_dist_cl_mem_->create_buffer( CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR )) {
      vcl_cerr << "\n ERROR: creating bocl_mem failed for OBJ_MIN_DIST" << vcl_endl;
      delete n_cam_cl_mem_;         delete n_obj_cl_mem_;
      if(is_grd_reg) { delete grd_id_cl_mem_;  delete grd_dist_cl_mem_;  delete grd_id_offset_cl_mem_;  delete grd_weight_cl_mem_; }
      if(is_obj_reg) { delete sky_id_cl_mem_;  delete sky_id_offset_cl_mem_;  delete sky_weight_cl_mem_; }
      delete obj_id_cl_mem_;        delete obj_id_offset_cl_mem_;  delete obj_weight_cl_mem_;
      delete obj_min_dist_cl_mem_;
      return false;
    }
    obj_order_cl_mem_ = new bocl_mem(gpu_->context(), obj_order_buff_, sizeof(unsigned char)*(*n_obj_), " obj_order " );
    if(!obj_order_cl_mem_->create_buffer( CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR )) {
      vcl_cerr << "\n ERROR: creating bocl_mem failed for OBJ_ORDER" << vcl_endl;
      delete n_cam_cl_mem_;         delete n_obj_cl_mem_;
      if(is_grd_reg) { delete grd_id_cl_mem_;  delete grd_dist_cl_mem_;  delete grd_id_offset_cl_mem_;  delete grd_weight_cl_mem_; }
      if(is_obj_reg) { delete sky_id_cl_mem_;  delete sky_id_offset_cl_mem_;  delete sky_weight_cl_mem_; }
      delete obj_id_cl_mem_;        delete obj_id_offset_cl_mem_;  delete obj_weight_cl_mem_;
      delete obj_min_dist_cl_mem_;  delete obj_order_cl_mem_;
      return false;
    }
    query_global_mem_ += sizeof(unsigned)*obj_vox_size;                          // object id
    query_global_mem_ += sizeof(unsigned)*query_->dist_offset().size();          // object offset
    query_global_mem_ += sizeof(float)*(*n_obj_);                                // object weight
    query_global_mem_ += sizeof(unsigned char)*2*(*n_obj_);                      // object dist and order
    query_local_mem_ += sizeof(unsigned char)*2*(*n_obj_);                       // object dist and order
    query_local_mem_ += sizeof(unsigned char)*2*(*n_obj_);                       // object dist and order
    query_local_mem_ += sizeof(float)*(2+*n_obj_);                               // grd, sky, object weight
  }

  // check the device memory
  // check whether query size has exceeded the available global memory and local_memory
  device_global_mem_ = (gpu_->info()).total_global_memory_;
  device_local_mem_ = (gpu_->info()).total_local_memory_;

  if (query_global_mem_ > device_global_mem_) {
    vcl_cerr << " ERROR: the required global memory for query "  << query_global_mem_/1073741824.0
             << " GByte is larger than available global memory " << device_global_mem_/1073741824.0 
             << " GB" << vcl_endl;
    this->clean_query_cl_mem();
    return false;
  }
  if (query_local_mem_ > device_local_mem_) {
    vcl_cerr << " ERROR: the required local memoery for query " << query_local_mem_/1024.0
             << " KByte is larger than available local memory " << device_local_mem_/1024.0
             << " KB" << vcl_endl;
    this->clean_query_cl_mem();
    return false;
  }
  
  return true;
}

bool boxm2_volm_matcher_p1::volm_matcher_p1_test(unsigned n_ind,
                                                 unsigned char* index,
                                                 float* score_buff,
                                                 float* mu_buff)
{
  vcl_vector<float> mu;
  unsigned nc = *n_cam_;
  unsigned no = *n_obj_;

  // calcualte mean depth value
  for (unsigned ind_id = 0; ind_id < n_ind; ind_id++) {
    unsigned start_ind = ind_id * (layer_size_);
    for (unsigned cam_id = 0; cam_id < nc; cam_id++) {
      for (unsigned k = 0; k < no; k++) {
        unsigned offset_id = k + no * cam_id;
        unsigned start_obj = obj_id_offset_buff_[offset_id];
        unsigned end_obj = obj_id_offset_buff_[offset_id+1];
        float    mu_obj = 0;
        unsigned cnt = 0;
        for (unsigned i = start_obj; i < end_obj; i++) {
          unsigned id = start_ind + obj_id_buff_[i];
          if ( (unsigned)index[id] < 253 ) {
            mu_obj += depth_interval_[index[id]];
            cnt += 1;
          }
        }
        mu_obj = (cnt > 0) ? mu_obj/cnt : 0;
        mu.push_back(mu_obj);
      }
    }
  }

  // calculate sky score
  vcl_vector<float> score_sky_all;
  for (unsigned ind_id = 0; ind_id < n_ind; ind_id++) {
    unsigned start_ind = ind_id * (layer_size_);
    for (unsigned cam_id = 0; cam_id < nc; cam_id++) {
      unsigned start = sky_id_offset_buff_[cam_id];
      unsigned end = sky_id_offset_buff_[cam_id+1];
      unsigned cnt = 0;
      for (unsigned k = start; k < end; k++) {
        unsigned id = start_ind + sky_id_buff_[k];
        if (index[id] == 254) cnt++;
      }
      float score_sky = (end != start) ? (float)cnt/(end-start) : 0.0f;
      score_sky *= (*sky_weight_buff_);
      score_sky_all.push_back(score_sky);
    }
  }

  // calculate the ground score
  vcl_vector<float> score_grd_all;
  for (unsigned ind_id = 0; ind_id < n_ind; ind_id++) {
    unsigned start_ind = ind_id * layer_size_;
    for (unsigned cam_id = 0; cam_id < nc; cam_id++) {
      unsigned start = grd_id_offset_buff_[cam_id];
      unsigned end = grd_id_offset_buff_[cam_id+1];
      unsigned cnt = 0;
      for (unsigned k = start; k < end; k++) {
        unsigned id = start_ind + grd_id_buff_[k];
        unsigned char ind_d = index[id];
        unsigned char grd_d = grd_dist_buff_[k];
        if ( ind_d >= (grd_d-1) && ind_d <= (grd_d+1) ) cnt++;
      }
      float score_grd = (end!=start) ? (float)cnt/(end-start) : 0.0f;
      score_grd *= (*grd_weight_buff_);
      score_grd_all.push_back(score_grd);
    }
  }

  // calculate the object score
  vcl_vector<float> score_order_all;
  vcl_vector<float> score_min_all;
  for (unsigned ind_id = 0; ind_id < n_ind; ind_id++) {
    unsigned start_ind = ind_id * layer_size_;
    for (unsigned cam_id = 0; cam_id < nc; cam_id++) {
      float score_order = 0.0f;
      float score_min = 0.0f;
      unsigned mu_start_id = cam_id*no + ind_id*no*nc;
      for (unsigned k = 0; k < no; k++) {
        unsigned offset_id = k + no*cam_id;
        unsigned start_obj = obj_id_offset_buff_[offset_id];
        unsigned end_obj = obj_id_offset_buff_[offset_id+1];
        float score_k = 0.0f;
        float score_min_k = 0.0f;
        for (unsigned i = start_obj; i < end_obj; i++) {
          unsigned id = start_ind + obj_id_buff_[i];
          unsigned d = index[id];
          unsigned s_vox = 1;
          unsigned s_min = 0;
          if ( d < 253 ){  // valid object voxel
            // do the order checking
            for (unsigned mu_id = 0; (mu_id < k && s_vox); mu_id++)
              s_vox = s_vox * ( depth_interval_[d] >= mu[mu_id+mu_start_id] );
            for (unsigned mu_id = k+1; (mu_id < no && s_vox); mu_id++)
              s_vox = s_vox * ( depth_interval_[d] <= mu[mu_id+mu_start_id] );
            if ( d > obj_min_dist_buff_[k] )
              s_min = 1;
          } else {
            s_vox = 0;
          }
          score_k += (float)s_vox;
          score_min_k += (float)s_min;
        } // end for loop over voxel in object k
        // normalized the order score for object k
        score_k = (end_obj != start_obj) ? score_k/(end_obj-start_obj) : 0;
        score_k = score_k * obj_weight_buff_[k];
        // normalized the min_dist score for object k
        score_min_k = (end_obj != start_obj) ? score_min_k/(end_obj-start_obj) : 0;
        score_min_k = score_min_k * obj_weight_buff_[k];
        // summerize order score for index ind_id and camera cam_id
        score_order += score_k;
        score_min += score_min_k; 
      }  // end for loop over objects
      score_order = score_order / no;
      score_order_all.push_back(score_order);
      score_min = score_min / no;
      score_min_all.push_back(score_min);
    } // end of loop over cameras
  } // end of loop over indices


  // get the overall score
  vcl_vector<float> score_all;
  for (unsigned ind_id = 0; ind_id < n_ind; ind_id++) {
    for (unsigned cam_id = 0; cam_id < nc; cam_id++) {
      unsigned id = cam_id + ind_id * nc;
      score_all.push_back(score_sky_all[id] + score_grd_all[id] + score_order_all[id] + score_min_all[id]);
    }
  }

  // output all sky and ground score for checking
#if 0
  for (unsigned ind_id = 0; ind_id < n_ind; ind_id++) {
    for (unsigned cam_id = 0; cam_id < nc; cam_id++) {
      unsigned id = cam_id + ind_id * nc;
      vcl_cout << " ind_id = " << ind_id << " cam_id = " << cam_id 
               << " score_sky[" << id << "] = " << score_sky_all[id] 
               << "\t\t score_grd[" << id << "] = " << score_grd_all[id]
               << vcl_endl;
    }
  }
#endif
  // output for objects
#if 1
  // mean values
  for (unsigned ind_id = 0; ind_id < n_ind; ind_id++)
    for (unsigned cam_id = 0; cam_id < nc; cam_id++)
      for (unsigned obj_id = 0; obj_id < no; obj_id++) {
        unsigned start_ind = ind_id * layer_size_;
        unsigned id = obj_id + cam_id * no + ind_id * nc * no;
        if( mu[id] - mu_buff[id]  != 0){
          vcl_cout << " ind_id = " << ind_id
                   << " cam_id = " << cam_id
                   << " obj_id = " << obj_id
                   << " start_ind = " << start_ind
                   << " id = " << id
                   << " mu_cpu = " << mu[id]
                   << " mu_gpu = " << mu_buff[id]
                   << " mu_diff = " << mu[id] - mu_buff[id]
                   << vcl_endl;
        }
      }


  // score_order
  //for (unsigned ind_id = 0; ind_id < n_ind; ind_id++) {
  //  for (unsigned cam_id = 0; cam_id < nc; cam_id++) {
  //    unsigned id = cam_id + ind_id * nc;
  //    vcl_cout << " ind_id = " << ind_id << " cam_id = " << cam_id << " score_order[" << id << "] = " << score_order_all[id] << vcl_endl;
  //  }
  //}

  // score_min
  //for (unsigned ind_id = 0; ind_id < n_ind; ind_id++) {
  //  for (unsigned cam_id = 0; cam_id < nc; cam_id++) {
  //    unsigned id = cam_id + ind_id * nc;
  //    vcl_cout << " ind = " << ind_id << " cam = " << cam_id << " score_min[" << id << "] = " << score_min_all[id] << vcl_endl;
  //  }
  //}

    // check the score output
  for (unsigned ind_id = 0; ind_id < n_ind; ind_id++) {
    for (unsigned cam_id = 0; cam_id < nc; cam_id++) {
      unsigned id = cam_id + ind_id * nc;
      vcl_cout << " ind = " << ind_id << " cam = " << cam_id << " id = " << id
               << "\t score_cpu = " << score_all[id]
               << "\t score_gpu = " << score_buff[id]
               << "\t diff = " << score_all[id] - score_buff[id]
               << vcl_endl;
    }
  }
#endif

  return true;
}