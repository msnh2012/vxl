// This is tbl/vepl2/vepl2_dyadic.cxx
#include "vepl2_dyadic.h"
#include <vepl2/accessors/vipl_accessors_vil_image_view_base.h>
#include <vipl/vipl_dyadic.h>
#include <vil/vil_image_view.h>
#include <vil/vil_pixel_format.h>
#include <vil/vil_rgb.h>
#include <vxl_config.h> // for vxl_byte

typedef vil_rgb<vxl_byte> r_g_b;
void sum_ubyte(vxl_byte& a, vxl_byte const& b) { a += b; }
void sum_short(vxl_uint_16& a, vxl_uint_16 const& b) { a += b; }
void sum_int(vxl_uint_32& a, vxl_uint_32 const& b) { a += b; }
void sum_rgb(r_g_b& a, r_g_b const& b) { a.r += b.r; a.g += b.g; a.b += b.b; }
void sum_float(float& a, float const& b) { a += b; }
void sum_double(double& a, double const& b) { a += b; }
void dif_ubyte(vxl_byte& a, vxl_byte const& b) { a -= b; }
void dif_short(vxl_uint_16& a, vxl_uint_16 const& b) { a -= b; }
void dif_int(vxl_uint_32& a, vxl_uint_32 const& b) { a -= b; }
void dif_rgb(r_g_b& a, r_g_b const& b) { a.r -= b.r; a.g -= b.g; a.b -= b.b; }
void dif_float(float& a, float const& b) { a -= b; }
void dif_double(double& a, double const& b) { a -= b; }
void min_ubyte(vxl_byte& a, vxl_byte const& b) { if (b<a) a = b; }
void min_short(vxl_uint_16& a, vxl_uint_16 const& b) { if (b<a) a = b; }
void min_int(vxl_uint_32& a, vxl_uint_32 const& b) { if (b<a) a = b; }
void min_float(float& a, float const& b) { if (b<a) a = b; }
void min_double(double& a, double const& b) { if (b<a) a = b; }
void max_ubyte(vxl_byte& a, vxl_byte const& b) { if (a<b) a = b; }
void max_short(vxl_uint_16& a, vxl_uint_16 const& b) { if (a<b) a = b; }
void max_int(vxl_uint_32& a, vxl_uint_32 const& b) { if (a<b) a = b; }
void max_float(float& a, float const& b) { if (a<b) a = b; }
void max_double(double& a, double const& b) { if (a<b) a = b; }

void vepl2_dyadic_sum(vil_image_view_base_sptr im_out, vil_image_view_base const& image)
{
  // byte greyscale
  if (image.pixel_format() == VIL_PIXEL_FORMAT_BYTE) {
    vil_image_view<vxl_byte>& out = static_cast<vil_image_view<vxl_byte>&>(*im_out);
    vipl_dyadic<vil_image_view_base,vil_image_view_base,vxl_byte,vxl_byte> op(sum_ubyte);
    op.put_in_data_ptr(&image);
    op.put_out_data_ptr(&out);
    op.filter();
  }

  // byte rgb
  else if (image.pixel_format() == VIL_PIXEL_FORMAT_RGB_BYTE) {
    vil_image_view<r_g_b >& out = static_cast<vil_image_view<r_g_b >&>(*im_out);
    vipl_dyadic<vil_image_view_base,vil_image_view_base,r_g_b ,r_g_b > op(sum_rgb);
    op.put_in_data_ptr(&image);
    op.put_out_data_ptr(&out);
    op.filter();
  }

  // short
  else if (image.pixel_format() == VIL_PIXEL_FORMAT_UINT_16) {
    vil_image_view<vxl_uint_16>& out = static_cast<vil_image_view<vxl_uint_16>&>(*im_out);
    vipl_dyadic<vil_image_view_base,vil_image_view_base,vxl_uint_16,vxl_uint_16> op(sum_short);
    op.put_in_data_ptr(&image);
    op.put_out_data_ptr(&out);
    op.filter();
  }

  // int
  else if (image.pixel_format() == VIL_PIXEL_FORMAT_UINT_32) {
    vil_image_view<vxl_uint_32>& out = static_cast<vil_image_view<vxl_uint_32>&>(*im_out);
    vipl_dyadic<vil_image_view_base,vil_image_view_base,vxl_uint_32,vxl_uint_32> op(sum_int);
    op.put_in_data_ptr(&image);
    op.put_out_data_ptr(&out);
    op.filter();
  }

  // float
  else if (image.pixel_format() == VIL_PIXEL_FORMAT_FLOAT) {
    vil_image_view<float>& out = static_cast<vil_image_view<float>&>(*im_out);
    vipl_dyadic<vil_image_view_base,vil_image_view_base,float,float> op(sum_float);
    op.put_in_data_ptr(&image);
    op.put_out_data_ptr(&out);
    op.filter();
  }

  // double
  else if (image.pixel_format() == VIL_PIXEL_FORMAT_DOUBLE) {
    vil_image_view<double>& out = static_cast<vil_image_view<double>&>(*im_out);
    vipl_dyadic<vil_image_view_base,vil_image_view_base,double,double> op(sum_double);
    op.put_in_data_ptr(&image);
    op.put_out_data_ptr(&out);
    op.filter();
  }

  //
  else {
    vcl_cerr << __FILE__ ": vepl2_dyadic_sum() not implemented for " << image.is_a() << '\n';
  }
}

void vepl2_dyadic_dif(vil_image_view_base_sptr im_out, vil_image_view_base const& image)
{
  // byte greyscale
  if (image.pixel_format() == VIL_PIXEL_FORMAT_BYTE) {
    vil_image_view<vxl_byte>& out = static_cast<vil_image_view<vxl_byte>&>(*im_out);
    vipl_dyadic<vil_image_view_base,vil_image_view_base,vxl_byte,vxl_byte> op(dif_ubyte);
    op.put_in_data_ptr(&image);
    op.put_out_data_ptr(&out);
    op.filter();
  }

  // byte rgb
  else if (image.pixel_format() == VIL_PIXEL_FORMAT_RGB_BYTE) {
    vil_image_view<r_g_b >& out = static_cast<vil_image_view<r_g_b >&>(*im_out);
    vipl_dyadic<vil_image_view_base,vil_image_view_base,r_g_b ,r_g_b > op(dif_rgb);
    op.put_in_data_ptr(&image);
    op.put_out_data_ptr(&out);
    op.filter();
  }

  // short
  else if (image.pixel_format() == VIL_PIXEL_FORMAT_UINT_16) {
    vil_image_view<vxl_uint_16>& out = static_cast<vil_image_view<vxl_uint_16>&>(*im_out);
    vipl_dyadic<vil_image_view_base,vil_image_view_base,vxl_uint_16,vxl_uint_16> op(dif_short);
    op.put_in_data_ptr(&image);
    op.put_out_data_ptr(&out);
    op.filter();
  }

  // int
  else if (image.pixel_format() == VIL_PIXEL_FORMAT_UINT_32) {
    vil_image_view<vxl_uint_32>& out = static_cast<vil_image_view<vxl_uint_32>&>(*im_out);
    vipl_dyadic<vil_image_view_base,vil_image_view_base,vxl_uint_32,vxl_uint_32> op(dif_int);
    op.put_in_data_ptr(&image);
    op.put_out_data_ptr(&out);
    op.filter();
  }

  // float
  else if (image.pixel_format() == VIL_PIXEL_FORMAT_FLOAT) {
    vil_image_view<float>& out = static_cast<vil_image_view<float>&>(*im_out);
    vipl_dyadic<vil_image_view_base,vil_image_view_base,float,float> op(dif_float);
    op.put_in_data_ptr(&image);
    op.put_out_data_ptr(&out);
    op.filter();
  }

  // double
  else if (image.pixel_format() == VIL_PIXEL_FORMAT_DOUBLE) {
    vil_image_view<double>& out = static_cast<vil_image_view<double>&>(*im_out);
    vipl_dyadic<vil_image_view_base,vil_image_view_base,double,double> op(dif_double);
    op.put_in_data_ptr(&image);
    op.put_out_data_ptr(&out);
    op.filter();
  }

  //
  else {
    vcl_cerr << __FILE__ ": vepl2_dyadic_dif() not implemented for " << image.is_a() << '\n';
  }
}

void vepl2_dyadic_min(vil_image_view_base_sptr im_out, vil_image_view_base const& image)
{
  // byte greyscale
  if (image.pixel_format() == VIL_PIXEL_FORMAT_BYTE) {
    vil_image_view<vxl_byte>& out = static_cast<vil_image_view<vxl_byte>&>(*im_out);
    vipl_dyadic<vil_image_view_base,vil_image_view_base,vxl_byte,vxl_byte> op(min_ubyte);
    op.put_in_data_ptr(&image);
    op.put_out_data_ptr(&out);
    op.filter();
  }

  // short
  else if (image.pixel_format() == VIL_PIXEL_FORMAT_UINT_16) {
    vil_image_view<vxl_uint_16>& out = static_cast<vil_image_view<vxl_uint_16>&>(*im_out);
    vipl_dyadic<vil_image_view_base,vil_image_view_base,vxl_uint_16,vxl_uint_16> op(min_short);
    op.put_in_data_ptr(&image);
    op.put_out_data_ptr(&out);
    op.filter();
  }

  // int
  else if (image.pixel_format() == VIL_PIXEL_FORMAT_UINT_32) {
    vil_image_view<vxl_uint_32>& out = static_cast<vil_image_view<vxl_uint_32>&>(*im_out);
    vipl_dyadic<vil_image_view_base,vil_image_view_base,vxl_uint_32,vxl_uint_32> op(min_int);
    op.put_in_data_ptr(&image);
    op.put_out_data_ptr(&out);
    op.filter();
  }

  // float
  else if (image.pixel_format() == VIL_PIXEL_FORMAT_FLOAT) {
    vil_image_view<float>& out = static_cast<vil_image_view<float>&>(*im_out);
    vipl_dyadic<vil_image_view_base,vil_image_view_base,float,float> op(min_float);
    op.put_in_data_ptr(&image);
    op.put_out_data_ptr(&out);
    op.filter();
  }

  // double
  else if (image.pixel_format() == VIL_PIXEL_FORMAT_DOUBLE) {
    vil_image_view<double>& out = static_cast<vil_image_view<double>&>(*im_out);
    vipl_dyadic<vil_image_view_base,vil_image_view_base,double,double> op(min_double);
    op.put_in_data_ptr(&image);
    op.put_out_data_ptr(&out);
    op.filter();
  }

  //
  else {
    vcl_cerr << __FILE__ ": vepl2_dyadic_min() not implemented for " << image.is_a() << '\n';
  }
}

void vepl2_dyadic_max(vil_image_view_base_sptr im_out, vil_image_view_base const& image)
{
  // byte greyscale
  if (image.pixel_format() == VIL_PIXEL_FORMAT_BYTE) {
    vil_image_view<vxl_byte>& out = static_cast<vil_image_view<vxl_byte>&>(*im_out);
    vipl_dyadic<vil_image_view_base,vil_image_view_base,vxl_byte,vxl_byte> op(max_ubyte);
    op.put_in_data_ptr(&image);
    op.put_out_data_ptr(&out);
    op.filter();
  }

  // short
  else if (image.pixel_format() == VIL_PIXEL_FORMAT_UINT_16) {
    vil_image_view<vxl_uint_16>& out = static_cast<vil_image_view<vxl_uint_16>&>(*im_out);
    vipl_dyadic<vil_image_view_base,vil_image_view_base,vxl_uint_16,vxl_uint_16> op(max_short);
    op.put_in_data_ptr(&image);
    op.put_out_data_ptr(&out);
    op.filter();
  }

  // int
  else if (image.pixel_format() == VIL_PIXEL_FORMAT_UINT_32) {
    vil_image_view<vxl_uint_32>& out = static_cast<vil_image_view<vxl_uint_32>&>(*im_out);
    vipl_dyadic<vil_image_view_base,vil_image_view_base,vxl_uint_32,vxl_uint_32> op(max_int);
    op.put_in_data_ptr(&image);
    op.put_out_data_ptr(&out);
    op.filter();
  }

  // float
  else if (image.pixel_format() == VIL_PIXEL_FORMAT_FLOAT) {
    vil_image_view<float>& out = static_cast<vil_image_view<float>&>(*im_out);
    vipl_dyadic<vil_image_view_base,vil_image_view_base,float,float> op(max_float);
    op.put_in_data_ptr(&image);
    op.put_out_data_ptr(&out);
    op.filter();
  }

  // double
  else if (image.pixel_format() == VIL_PIXEL_FORMAT_DOUBLE) {
    vil_image_view<double>& out = static_cast<vil_image_view<double>&>(*im_out);
    vipl_dyadic<vil_image_view_base,vil_image_view_base,double,double> op(max_double);
    op.put_in_data_ptr(&image);
    op.put_out_data_ptr(&out);
    op.filter();
  }

  //
  else
    vcl_cerr << __FILE__ ": vepl2_dyadic_max() not implemented for " << image.is_a() << '\n';
}

