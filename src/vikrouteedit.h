/*
 * viking -- GPS Data and Topo Analyzer, Explorer, and Manager
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 or later.
 */
#ifndef _VIKING_ROUTE_EDIT_H
#define _VIKING_ROUTE_EDIT_H

#include "viktrack.h"

typedef struct _VikWaypoint VikWaypoint;

G_BEGIN_DECLS

/*
 * The callback returns newly generated interior points for the requested
 * section.  It owns the returned list on success; on failure it must leave
 * the output list NULL.
 */
typedef gboolean (*VikRouteEditRouteFunc) ( gpointer user_data,
                                            const VikCoord *start,
                                            const VikCoord *end,
                                            GList **generated );

gboolean vik_route_edit_move_point ( VikTrack *track,
                                     GList *point_link,
                                     const VikCoord *new_coord,
                                     VikRouteEditRouteFunc route_func,
                                     gpointer user_data );

gboolean vik_route_edit_delete_point ( VikTrack *track,
                                       GList *point_link,
                                       VikRouteEditRouteFunc route_func,
                                       gpointer user_data,
                                       GList **selected_link );

gboolean vik_route_edit_insert_point ( VikTrack *track,
                                       VikTrackpoint *point,
                                       VikRouteEditRouteFunc route_func,
                                       gpointer user_data,
                                       GList **new_point_link );

VikTrackpoint *vik_route_edit_trackpoint_from_waypoint ( const VikWaypoint *waypoint );

G_END_DECLS

#endif
