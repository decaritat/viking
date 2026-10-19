#include <math.h>

#include "vikrouteedit.h"
#include "vikwaypoint.h"

typedef struct {
  guint calls;
  guint fail_at;
} FakeRouter;

static VikCoord coord ( gdouble lat, gdouble lon )
{
  VikCoord result;
  result.mode = VIK_COORD_LATLON;
  result.north_south = lat;
  result.east_west = lon;
  result.utm_zone = 0;
  result.utm_letter = 0;
  return result;
}

static VikTrackpoint *add_point ( VikTrack *track, gdouble lat, gdouble lon, gboolean newsegment )
{
  VikTrackpoint *point = vik_trackpoint_new ();
  point->coord = coord ( lat, lon );
  point->newsegment = newsegment;
  vik_track_add_trackpoint ( track, point, FALSE );
  return point;
}

static guint point_count ( VikTrack *track )
{
  return g_list_length ( track->trackpoints );
}

static GList *point_link ( VikTrack *track, VikTrackpoint *point )
{
  return g_list_find ( track->trackpoints, point );
}

static gboolean fake_route ( gpointer user_data, const VikCoord *start,
                             const VikCoord *end, GList **generated )
{
  FakeRouter *router = user_data;
  router->calls++;
  if ( router->fail_at && router->calls == router->fail_at )
    return FALSE;

  VikTrackpoint *point = vik_trackpoint_new ();
  point->coord = coord ( (start->north_south + end->north_south) / 2.0,
                         (start->east_west + end->east_west) / 2.0 );
  point->newsegment = TRUE;
  *generated = g_list_append ( NULL, point );
  return TRUE;
}

static VikTrack *three_point_route ( VikTrackpoint **first, VikTrackpoint **middle,
                                     VikTrackpoint **last )
{
  VikTrack *track = vik_track_new ();
  track->is_route = TRUE;
  *first = add_point ( track, 0.0, 0.0, TRUE );
  *middle = add_point ( track, 0.0, 2.0, FALSE );
  *last = add_point ( track, 0.0, 4.0, FALSE );
  return track;
}

static void test_move_middle_preserves_metadata ( void )
{
  VikTrackpoint *first, *middle, *last;
  VikTrack *track = three_point_route ( &first, &middle, &last );
  GList *middle_link = point_link ( track, middle );
  FakeRouter router = { 0, 0 };
  VikCoord destination = coord ( 1.0, 2.0 );

  middle->timestamp = 123.0;
  middle->altitude = 456.0;
  middle->speed = 7.0;
  middle->nsats = 9;
  vik_trackpoint_set_name ( middle, "manual" );
  vik_trackpoint_set_extensions ( middle, "<manual/>" );

  g_assert_true ( vik_route_edit_move_point ( track, middle_link, &destination,
                                              fake_route, &router ) );
  g_assert_cmpuint ( router.calls, ==, 2 );
  g_assert_true ( vik_coord_equalish ( &middle->coord, &destination ) );
  g_assert_cmpfloat ( middle->timestamp, ==, 123.0 );
  g_assert_cmpfloat ( middle->altitude, ==, 456.0 );
  g_assert_cmpfloat ( middle->speed, ==, 7.0 );
  g_assert_cmpint ( middle->nsats, ==, 9 );
  g_assert_cmpstr ( middle->name, ==, "manual" );
  g_assert_cmpstr ( middle->extensions, ==, "<manual/>" );
  g_assert_true ( g_list_find ( track->trackpoints, first ) != NULL );
  g_assert_true ( g_list_find ( track->trackpoints, last ) != NULL );
  g_assert_cmpuint ( point_count ( track ), ==, 5 );
  g_assert_cmpfloat ( track->bbox.north, ==, 1.0 );
  g_assert_cmpfloat ( track->bbox.south, ==, 0.0 );

  vik_track_free ( track );
}

static void test_move_first_and_last ( void )
{
  VikTrackpoint *first, *middle, *last;
  VikTrack *track = three_point_route ( &first, &middle, &last );
  FakeRouter router = { 0, 0 };
  VikCoord first_destination = coord ( 1.0, 0.0 );
  VikCoord last_destination = coord ( 0.0, 5.0 );

  g_assert_true ( vik_route_edit_move_point ( track, point_link ( track, first ),
                                              &first_destination, fake_route, &router ) );
  g_assert_cmpuint ( router.calls, ==, 1 );
  g_assert_true ( first->newsegment );
  g_assert_true ( vik_coord_equalish ( &first->coord, &first_destination ) );

  g_assert_true ( vik_route_edit_move_point ( track, point_link ( track, last ),
                                              &last_destination, fake_route, &router ) );
  g_assert_cmpuint ( router.calls, ==, 2 );
  g_assert_true ( vik_coord_equalish ( &last->coord, &last_destination ) );
  g_assert_cmpfloat ( track->bbox.north, ==, 1.0 );
  g_assert_cmpfloat ( track->bbox.east, ==, 5.0 );

  vik_track_free ( track );
}

static void test_delete_middle_reroutes ( void )
{
  VikTrackpoint *first, *middle, *last;
  VikTrack *track = three_point_route ( &first, &middle, &last );
  FakeRouter router = { 0, 0 };
  GList *selected = NULL;

  g_assert_true ( vik_route_edit_delete_point ( track, point_link ( track, middle ),
                                                fake_route, &router, &selected ) );
  g_assert_cmpuint ( router.calls, ==, 1 );
  g_assert_cmpuint ( point_count ( track ), ==, 3 );
  g_assert_true ( selected != NULL );
  g_assert_true ( selected->data == last );
  g_assert_true ( g_list_find ( track->trackpoints, first ) != NULL );
  g_assert_true ( g_list_find ( track->trackpoints, middle ) == NULL );
  g_assert_cmpfloat ( track->bbox.east, ==, 4.0 );

  vik_track_free ( track );
}

static void test_delete_segment_boundaries_and_short_routes ( void )
{
  VikTrack *track = vik_track_new ();
  track->is_route = TRUE;
  VikTrackpoint *first = add_point ( track, 0.0, 0.0, TRUE );
  VikTrackpoint *second = add_point ( track, 0.0, 1.0, FALSE );
  VikTrackpoint *segment_start = add_point ( track, 0.0, 2.0, TRUE );
  VikTrackpoint *segment_end = add_point ( track, 0.0, 3.0, FALSE );
  FakeRouter router = { 0, 0 };
  GList *selected = NULL;

  g_assert_true ( vik_route_edit_delete_point ( track, point_link ( track, first ),
                                                fake_route, &router, &selected ) );
  g_assert_true ( VIK_TRACKPOINT(track->trackpoints->data) == second );
  g_assert_true ( second->newsegment );
  g_assert_cmpuint ( router.calls, ==, 0 );

  g_assert_true ( vik_route_edit_delete_point ( track, point_link ( track, segment_start ),
                                                fake_route, &router, &selected ) );
  g_assert_true ( segment_end->newsegment );
  g_assert_cmpuint ( router.calls, ==, 0 );

  g_assert_true ( vik_route_edit_delete_point ( track, point_link ( track, segment_end ),
                                                fake_route, &router, &selected ) );
  g_assert_cmpuint ( point_count ( track ), ==, 1 );
  g_assert_true ( selected != NULL && selected->data == second );

  g_assert_true ( vik_route_edit_delete_point ( track, selected,
                                                fake_route, &router, &selected ) );
  g_assert_cmpuint ( point_count ( track ), ==, 0 );
  g_assert_null ( selected );
  vik_track_free ( track );
}

static void test_insert_waypoint_point ( void )
{
  VikTrack *track = vik_track_new ();
  track->is_route = TRUE;
  VikTrackpoint *first = add_point ( track, 0.0, 0.0, TRUE );
  VikTrackpoint *last = add_point ( track, 0.0, 4.0, FALSE );
  VikTrackpoint *inserted = vik_trackpoint_new ();
  inserted->coord = coord ( 0.0, 2.0 );
  inserted->timestamp = 99.0;
  inserted->altitude = 12.0;
  vik_trackpoint_set_name ( inserted, "waypoint" );
  FakeRouter router = { 0, 0 };
  GList *new_link = NULL;

  g_assert_true ( vik_route_edit_insert_point ( track, inserted,
                                                fake_route, &router, &new_link ) );
  g_assert_cmpuint ( router.calls, ==, 2 );
  g_assert_nonnull ( new_link );
  g_assert_true ( new_link->data == inserted );
  g_assert_cmpuint ( point_count ( track ), ==, 5 );
  g_assert_true ( first->newsegment );
  g_assert_false ( inserted->newsegment );
  g_assert_cmpfloat ( inserted->timestamp, ==, 99.0 );
  g_assert_cmpfloat ( inserted->altitude, ==, 12.0 );
  g_assert_cmpstr ( inserted->name, ==, "waypoint" );
  g_assert_true ( g_list_find ( track->trackpoints, last ) != NULL );

  vik_track_free ( track );
}

static void test_empty_and_one_point_routes ( void )
{
  VikTrack *track = vik_track_new ();
  track->is_route = TRUE;
  VikTrackpoint *point = vik_trackpoint_new ();
  point->coord = coord ( 2.0, 3.0 );
  GList *new_link = NULL;
  GList *selected = NULL;
  FakeRouter router = { 0, 0 };
  VikCoord destination = coord ( 4.0, 5.0 );

  g_assert_true ( vik_route_edit_insert_point ( track, point, fake_route,
                                                &router, &new_link ) );
  g_assert_true ( point->newsegment );
  g_assert_cmpuint ( router.calls, ==, 0 );
  g_assert_true ( vik_route_edit_move_point ( track, new_link, &destination,
                                              fake_route, &router ) );
  g_assert_cmpuint ( router.calls, ==, 0 );
  g_assert_true ( vik_coord_equalish ( &point->coord, &destination ) );
  g_assert_true ( vik_route_edit_delete_point ( track, new_link, fake_route,
                                                &router, &selected ) );
  g_assert_cmpuint ( point_count ( track ), ==, 0 );
  g_assert_null ( selected );

  VikTrackpoint *one_point = add_point ( track, 6.0, 7.0, TRUE );
  GList *one_link = point_link ( track, one_point );
  g_assert_true ( vik_route_edit_move_point ( track, one_link, &destination,
                                              fake_route, &router ) );
  g_assert_true ( vik_route_edit_delete_point ( track, one_link, fake_route,
                                                &router, &selected ) );
  g_assert_cmpuint ( point_count ( track ), ==, 0 );
  g_assert_null ( selected );
  vik_track_free ( track );
}

static void test_waypoint_metadata_copy ( void )
{
  VikWaypoint *waypoint = vik_waypoint_new ();
  waypoint->coord = coord ( 1.0, 2.0 );
  waypoint->timestamp = 10.0;
  waypoint->altitude = 20.0;
  waypoint->course = 30.0;
  waypoint->speed = 40.0;
  waypoint->nsats = 5;
  waypoint->fix_mode = 2;
  waypoint->hdop = 1.0;
  waypoint->vdop = 2.0;
  waypoint->pdop = 3.0;
  vik_waypoint_set_name ( waypoint, "waypoint" );
  vik_waypoint_set_extensions ( waypoint, "<wptx/>" );

  VikTrackpoint *point = vik_route_edit_trackpoint_from_waypoint ( waypoint );
  g_assert_nonnull ( point );
  g_assert_true ( vik_coord_equalish ( &point->coord, &waypoint->coord ) );
  g_assert_cmpfloat ( point->timestamp, ==, waypoint->timestamp );
  g_assert_cmpfloat ( point->altitude, ==, waypoint->altitude );
  g_assert_cmpfloat ( point->course, ==, waypoint->course );
  g_assert_cmpfloat ( point->speed, ==, waypoint->speed );
  g_assert_cmpuint ( point->nsats, ==, waypoint->nsats );
  g_assert_cmpuint ( point->fix_mode, ==, waypoint->fix_mode );
  g_assert_cmpfloat ( point->hdop, ==, waypoint->hdop );
  g_assert_cmpfloat ( point->vdop, ==, waypoint->vdop );
  g_assert_cmpfloat ( point->pdop, ==, waypoint->pdop );
  g_assert_cmpstr ( point->name, ==, waypoint->name );
  g_assert_cmpstr ( point->extensions, ==, waypoint->extensions );

  vik_trackpoint_free ( point );
  vik_waypoint_free ( waypoint );
}

static void test_failed_edits_are_atomic ( void )
{
  VikTrackpoint *first, *middle, *last;
  VikTrack *track = three_point_route ( &first, &middle, &last );
  GList *middle_link = point_link ( track, middle );
  GList *original_points = g_list_copy ( track->trackpoints );
  FakeRouter router = { 0, 2 };
  VikCoord destination = coord ( 1.0, 2.0 );
  GList *selected = NULL;

  middle->timestamp = 17.0;
  g_assert_false ( vik_route_edit_move_point ( track, middle_link, &destination,
                                               fake_route, &router ) );
  g_assert_true ( vik_coord_equalish ( &middle->coord, &(VikCoord){ .mode = VIK_COORD_LATLON, .north_south = 0.0, .east_west = 2.0 } ) );
  g_assert_cmpuint ( point_count ( track ), ==, 3 );
  g_assert_true ( track->trackpoints->data == original_points->data );
  g_assert_cmpfloat ( middle->timestamp, ==, 17.0 );

  router.calls = 0;
  router.fail_at = 1;
  g_assert_false ( vik_route_edit_delete_point ( track, middle_link,
                                                 fake_route, &router, &selected ) );
  g_assert_cmpuint ( point_count ( track ), ==, 3 );
  g_assert_null ( selected );

  VikTrackpoint *inserted = vik_trackpoint_new ();
  inserted->coord = coord ( 0.0, 1.0 );
  router.calls = 0;
  GList *new_link = NULL;
  g_assert_false ( vik_route_edit_insert_point ( track, inserted,
                                                 fake_route, &router, &new_link ) );
  g_assert_cmpuint ( point_count ( track ), ==, 3 );
  g_assert_null ( new_link );

  g_list_free ( original_points );
  vik_track_free ( track );
}

int main ( int argc, char **argv )
{
  g_test_init ( &argc, &argv, NULL );
  g_test_add_func ( "/route-edit/move-middle", test_move_middle_preserves_metadata );
  g_test_add_func ( "/route-edit/move-boundaries", test_move_first_and_last );
  g_test_add_func ( "/route-edit/delete-middle", test_delete_middle_reroutes );
  g_test_add_func ( "/route-edit/delete-boundaries", test_delete_segment_boundaries_and_short_routes );
  g_test_add_func ( "/route-edit/insert", test_insert_waypoint_point );
  g_test_add_func ( "/route-edit/short-routes", test_empty_and_one_point_routes );
  g_test_add_func ( "/route-edit/waypoint-metadata", test_waypoint_metadata_copy );
  g_test_add_func ( "/route-edit/rollback", test_failed_edits_are_atomic );
  return g_test_run ();
}
