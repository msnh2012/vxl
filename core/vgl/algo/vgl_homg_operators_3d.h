#ifndef vgl_homg_operators_3d_h
#define vgl_homg_operators_3d_h
#ifdef __GNUC__
#pragma interface
#endif

// Author: Don Hamilton, Peter Tu
// Copyright:
// Created: Feb 16 2000
//: 3D homogeneous operations

#include <vcl/vcl_vector.h>
#include <vnl/vnl_vector.h>
#include <vgl/vgl_homg_line_3d_2_points.h>

template <class Type> class vgl_homg_point_3d;
template <class Type> class vgl_homg_line_3d;
template <class Type> class vgl_homg_plane_3d;

template <class Type>
class vgl_homg_operators_3d {

public:
  
  // method to get a vnl_vector rep of a homogeneous object
  
  static vnl_vector<Type> get_vector(vgl_homg_point_3d<Type> &p);
  static vnl_vector<Type> get_vector(vgl_homg_plane_3d<Type> &p);
  


  static double angle_between_oriented_lines (const vgl_homg_line_3d<Type>& line1, const vgl_homg_line_3d<Type>& line2);
  static Type distance_squared (const vgl_homg_point_3d<Type>& point1, const vgl_homg_point_3d<Type>& point2);
  static vgl_homg_point_3d<Type> intersect_line_and_plane (const vgl_homg_line_3d<Type>&, const vgl_homg_plane_3d<Type>&);
  static vgl_homg_point_3d<Type> lines_to_point (const vgl_homg_line_3d<Type>& line1, const vgl_homg_line_3d<Type>& line2);
  static vgl_homg_point_3d<Type> lines_to_point (const vcl_vector<vgl_homg_line_3d<Type> >& line_list);
  static double perp_distance_squared (const vgl_homg_line_3d<Type>& line, const vgl_homg_point_3d<Type>& point);
  static vgl_homg_line_3d<Type> perp_line_through_point (const vgl_homg_line_3d<Type>& line, const vgl_homg_point_3d<Type>& point);
  static vgl_homg_point_3d<Type> perp_projection (const vgl_homg_line_3d<Type>& line, const vgl_homg_point_3d<Type>& point);
  static vgl_homg_line_3d<Type> planes_to_line (const vgl_homg_plane_3d<Type>& plane1, const vgl_homg_plane_3d<Type>& plane2);
  static vgl_homg_line_3d<Type> planes_to_line (const vcl_vector<vgl_homg_plane_3d<Type> >& plane_list);
  static vgl_homg_line_3d<Type> points_to_line (const vgl_homg_point_3d<Type>& point1, const vgl_homg_point_3d<Type>& point2);
  static vgl_homg_line_3d<Type> points_to_line (const vcl_vector<vgl_homg_point_3d<Type> >& point_list);

  static vgl_homg_plane_3d<Type> points_to_plane (const vgl_homg_point_3d<Type>& , const vgl_homg_point_3d<Type>& , const vgl_homg_point_3d<Type>& );
  static vgl_homg_plane_3d<Type> points_to_plane (const vcl_vector<vgl_homg_point_3d<Type> >& point_list);
  static vgl_homg_point_3d<Type> intersection_point (const vgl_homg_plane_3d<Type>& , const vgl_homg_plane_3d<Type>& , const vgl_homg_plane_3d<Type>& );
  static vgl_homg_point_3d<Type> intersection_point (const vcl_vector<vgl_homg_plane_3d<Type> >&);

    
  // cross ratio of four colinear points, or four planes through a common line
  static double CrossRatio(const vgl_homg_point_3d<Type>& p1, const vgl_homg_point_3d<Type>& p2,
                           const vgl_homg_point_3d<Type>& p3, const vgl_homg_point_3d<Type>& p4);
};

#endif // _vgl_homg_operators_3d_h
