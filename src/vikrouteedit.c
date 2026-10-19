/*
 * viking -- GPS Data and Topo Analyzer, Explorer, and Manager
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 or later.
 */
#include "viking.h"
#include "vikrouteedit.h"
#include "vikwaypoint.h"

typedef struct {
  GList *start;
  GList *end;
  GList *generated;
} RouteEditSection;

static void free_route_points ( GList *points )
{
  g_list_free_full ( points, (GDestroyNotify) vik_trackpoint_free );
}

static void replace_route_section ( VikTrack *track, RouteEditSection *section )
{
  GList *iter;

  if ( section->start )
    iter = section->start->next;
  else
    iter = track->trackpoints;

  while ( iter && iter != section->end ) {
    GList *next = iter->next;
    vik_trackpoint_free ( iter->data );
    track->trackpoints = g_list_delete_link ( track->trackpoints, iter );
    iter = next;
  }

  for ( iter = section->generated; iter; iter = iter->next ) {
    if ( section->end )
      track->trackpoints = g_list_insert_before ( track->trackpoints, section->end, iter->data );
    else
      track->trackpoints = g_list_append ( track->trackpoints, iter->data );
  }
  g_list_free ( section->generated );
  section->generated = NULL;
}

static gboolean route_section ( VikRouteEditRouteFunc route_func, gpointer user_data,
                                const VikCoord *start, const VikCoord *end,
                                GList **generated )
{
  g_return_val_if_fail ( generated != NULL, FALSE );
  *generated = NULL;

  if ( vik_coord_equalish ( start, end ) )
    return TRUE;

  if ( !route_func || !route_func ( user_data, start, end, generated ) )
    return FALSE;

  for ( GList *iter = *generated; iter; iter = iter->next )
    VIK_TRACKPOINT(iter->data)->newsegment = FALSE;
  return TRUE;
}

VikTrackpoint *vik_route_edit_trackpoint_from_waypoint ( const VikWaypoint *waypoint )
{
  VikTrackpoint *point;

  g_return_val_if_fail ( waypoint != NULL, NULL );

  point = vik_trackpoint_new ();
  point->coord = waypoint->coord;
  point->timestamp = waypoint->timestamp;
  point->altitude = waypoint->altitude;
  point->course = waypoint->course;
  point->speed = waypoint->speed;
  point->nsats = waypoint->nsats;
  point->fix_mode = waypoint->fix_mode;
  point->hdop = waypoint->hdop;
  point->vdop = waypoint->vdop;
  point->pdop = waypoint->pdop;
  vik_trackpoint_set_name ( point, waypoint->name );
  vik_trackpoint_set_extensions ( point, waypoint->extensions );
  return point;
}

gboolean vik_route_edit_move_point ( VikTrack *track,
                                     GList *point_link,
                                     const VikCoord *new_coord,
                                     VikRouteEditRouteFunc route_func,
                                     gpointer user_data )
{
  VikTrackpoint *point;
  RouteEditSection sections[2];
  guint section_count = 0;
  GList *generated = NULL;

  if ( !track || !track->is_route || !point_link || !new_coord )
    return FALSE;

  point = VIK_TRACKPOINT ( point_link->data );

  if ( point_link->prev && !point->newsegment ) {
    sections[section_count].start = point_link->prev;
    sections[section_count].end = point_link;
    sections[section_count].generated = NULL;
    if ( !route_section ( route_func, user_data,
                          &VIK_TRACKPOINT(point_link->prev->data)->coord,
                          new_coord, &generated ) )
      return FALSE;
    sections[section_count++].generated = generated;
  }

  if ( point_link->next && !VIK_TRACKPOINT(point_link->next->data)->newsegment ) {
    sections[section_count].start = point_link;
    sections[section_count].end = point_link->next;
    sections[section_count].generated = NULL;
    if ( !route_section ( route_func, user_data,
                          new_coord,
                          &VIK_TRACKPOINT(point_link->next->data)->coord,
                          &generated ) ) {
      for ( guint i = 0; i < section_count; i++ )
        free_route_points ( sections[i].generated );
      return FALSE;
    }
    sections[section_count++].generated = generated;
  }

  point->coord = *new_coord;
  for ( guint i = 0; i < section_count; i++ )
    replace_route_section ( track, &sections[i] );
  vik_track_calculate_bounds ( track );
  return TRUE;
}

gboolean vik_route_edit_delete_point ( VikTrack *track,
                                       GList *point_link,
                                       VikRouteEditRouteFunc route_func,
                                       gpointer user_data,
                                       GList **selected_link )
{
  VikTrackpoint *point;
  GList *previous;
  GList *next;
  RouteEditSection section;
  GList *generated = NULL;

  g_return_val_if_fail ( selected_link != NULL, FALSE );
  *selected_link = NULL;

  if ( !track || !track->is_route || !point_link )
    return FALSE;

  point = VIK_TRACKPOINT ( point_link->data );
  previous = point_link->prev;
  next = point_link->next;

  /*
   * A newsegment marker belongs to the point at the start of a segment.
   * Never route across either side of such a marker.
   */
  if ( previous && next && !point->newsegment &&
       !VIK_TRACKPOINT(next->data)->newsegment ) {
    section.start = previous;
    section.end = next;
    section.generated = NULL;
    if ( !route_section ( route_func, user_data,
                          &VIK_TRACKPOINT(previous->data)->coord,
                          &VIK_TRACKPOINT(next->data)->coord,
                          &generated ) )
      return FALSE;
    section.generated = generated;
    replace_route_section ( track, &section );
    *selected_link = next;
  }
  else {
    GList *replacement = next ? next : previous;
    if ( point->newsegment && next )
      VIK_TRACKPOINT(next->data)->newsegment = TRUE;
    track->trackpoints = g_list_delete_link ( track->trackpoints, point_link );
    vik_trackpoint_free ( point );
    *selected_link = replacement;
  }

  vik_track_calculate_bounds ( track );
  return TRUE;
}

gboolean vik_route_edit_insert_point ( VikTrack *track,
                                       VikTrackpoint *point,
                                       VikRouteEditRouteFunc route_func,
                                       gpointer user_data,
                                       GList **new_point_link )
{
  GList *after = NULL;
  GList *before = NULL;
  GList *iter;
  gdouble best = G_MAXDOUBLE;
  RouteEditSection sections[2];
  guint section_count = 0;
  GList *generated = NULL;

  g_return_val_if_fail ( new_point_link != NULL, FALSE );
  *new_point_link = NULL;

  if ( !track || !track->is_route || !point )
    return FALSE;

  if ( !track->trackpoints ) {
    point->newsegment = TRUE;
    track->trackpoints = g_list_append ( track->trackpoints, point );
    *new_point_link = track->trackpoints;
    vik_track_calculate_bounds ( track );
    return TRUE;
  }

  /*
   * Insert into the closest connected segment.  Segment markers are on the
   * point starting a segment, so a marker on the candidate end excludes it.
   */
  for ( iter = track->trackpoints; iter && iter->next; iter = iter->next ) {
    VikTrackpoint *left = VIK_TRACKPOINT ( iter->data );
    VikTrackpoint *right = VIK_TRACKPOINT ( iter->next->data );
    if ( right->newsegment )
      continue;
    gdouble distance = vik_coord_diff ( &left->coord, &point->coord ) +
                       vik_coord_diff ( &point->coord, &right->coord );
    if ( distance < best ) {
      best = distance;
      after = iter;
      before = iter->next;
    }
  }
  if ( !after ) {
    after = g_list_last ( track->trackpoints );
    before = NULL;
  }

  sections[section_count].start = after;
  sections[section_count].end = NULL;
  sections[section_count].generated = NULL;
  if ( !route_section ( route_func, user_data,
                        &VIK_TRACKPOINT(after->data)->coord,
                        &point->coord, &generated ) ) {
    vik_trackpoint_free ( point );
    return FALSE;
  }
  sections[section_count++].generated = generated;

  if ( before ) {
    sections[section_count].start = NULL;
    sections[section_count].end = before;
    sections[section_count].generated = NULL;
    if ( !route_section ( route_func, user_data, &point->coord,
                          &VIK_TRACKPOINT(before->data)->coord, &generated ) ) {
      for ( guint i = 0; i < section_count; i++ )
        free_route_points ( sections[i].generated );
      vik_trackpoint_free ( point );
      return FALSE;
    }
    sections[section_count++].generated = generated;
  }

  track->trackpoints = before
    ? g_list_insert_before ( track->trackpoints, before, point )
    : g_list_append ( track->trackpoints, point );
  *new_point_link = g_list_find ( track->trackpoints, point );

  sections[0].end = *new_point_link;
  if ( section_count > 1 )
    sections[1].start = *new_point_link;
  for ( guint i = 0; i < section_count; i++ )
    replace_route_section ( track, &sections[i] );
  vik_track_calculate_bounds ( track );
  return TRUE;
}
