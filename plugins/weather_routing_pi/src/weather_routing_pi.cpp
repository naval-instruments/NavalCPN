/***************************************************************************
 *
 * Project:  OpenCPN Weather Routing plugin
 * Author:   Sean D'Epagnier
 *
 ***************************************************************************
 *   Copyright (C) 2016 by Sean D'Epagnier                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 ***************************************************************************
 */

#include <wx/wx.h>
#include <wx/stdpaths.h>
#include <wx/treectrl.h>
#include <wx/fileconf.h>

#include "Utilities.h"
#include "Boat.h"
#include "RouteMapOverlay.h"
#include "WeatherRouting.h"
#include "weather_routing_pi.h"

// Define minimum and maximum versions of the grib plugin supported
#define GRIB_MAX_MAJOR 4
#define GRIB_MAX_MINOR 1
#define GRIB_MIN_MAJOR 4
#define GRIB_MIN_MINOR 1

//Define minimum and maximum versions of the climatology plugin supported
#define CLIMATOLOGY_MAX_MAJOR 1
#define CLIMATOLOGY_MAX_MINOR 4
#define CLIMATOLOGY_MIN_MAJOR 0
#define CLIMATOLOGY_MIN_MINOR 10

static Json::Value g_ReceivedODVersionJSONMsg;
static bool ODVersionNewerThan(int major, int minor, int patch)
{
    Json::Value jMsg;
    Json::FastWriter writer;
    jMsg["Source"] = "WEATHER_ROUTING_PI";
    jMsg["Type"] = "Request";
    jMsg["Msg"] = "Version";
    jMsg["MsgId"] = "version";
    SendPluginMessage( wxS("OCPN_DRAW_PI"), writer.write( jMsg ) );

    if(g_ReceivedODVersionJSONMsg.size() <= 0)
        return false;
    if(g_ReceivedODVersionJSONMsg["Major"].asInt() > major) return true;
    if(g_ReceivedODVersionJSONMsg["Major"].asInt() == major &&
        g_ReceivedODVersionJSONMsg["Minor"].asInt() > minor) return true;
    if(g_ReceivedODVersionJSONMsg["Major"].asInt() == major &&
        g_ReceivedODVersionJSONMsg["Minor"].asInt() == minor &&
        g_ReceivedODVersionJSONMsg["Patch"].asInt() >= patch) return true;
    return false;
}


extern "C" DECL_EXP opencpn_plugin* create_pi(void *ppimgr)
{
    return new weather_routing_pi(ppimgr);
}

extern "C" DECL_EXP void destroy_pi(opencpn_plugin* p)
{
    delete p;
}

#include "icons.h"

weather_routing_pi::weather_routing_pi(void *ppimgr)
      :opencpn_plugin_115(ppimgr)
{
      // Create the PlugIn icons
      initialize_images();
      b_in_boundary_reply = false;
      m_tCursorLatLon.Connect(wxEVT_TIMER, wxTimerEventHandler
                              ( weather_routing_pi::OnCursorLatLonTimer ), NULL, this);
}

weather_routing_pi::~weather_routing_pi(void)
{
      delete _img_WeatherRouting;
}

int weather_routing_pi::Init(void)
{
      AddLocaleCatalog( _T("opencpn-weather_routing_pi") );

      //    Get a pointer to the opencpn configuration object
      m_pconfig = GetOCPNConfigObject();

      // Get a pointer to the opencpn display canvas, to use as a parent for the WEATHER_ROUTING dialog
      m_parent_window = GetOCPNCanvasWindow();

      m_pWeather_Routing = NULL;

#ifdef OCPN_USE_SVG
      m_leftclick_tool_id = InsertPlugInToolSVG(_T( "WeatherRouting" ),
          _svg_weather_routing, _svg_weather_routing_rollover, _svg_weather_routing_toggled,
          wxITEM_CHECK, _("Weather Routing"), _T( "" ), NULL, WEATHER_ROUTING_TOOL_POSITION, 0, this);
#else
      m_leftclick_tool_id  = InsertPlugInTool
          (_T(""), _img_WeatherRouting, _img_WeatherRouting, wxITEM_CHECK,
           _("Weather Routing"), _T(""), NULL,
           WEATHER_ROUTING_TOOL_POSITION, 0, this);
#endif
      wxMenu dummy_menu;
      m_position_menu_id = AddCanvasContextMenuItem
          (new wxMenuItem(&dummy_menu, -1, _("Weather Route Position")), this );
      SetCanvasMenuItemViz(m_position_menu_id, false);

      m_waypoint_menu_id = AddCanvasMenuItem (new wxMenuItem(&dummy_menu, -1, _("Weather Route Position")), this, "Waypoint" );
      SetCanvasMenuItemViz(m_waypoint_menu_id, false, "Waypoint");

      //    And load the configuration items
      LoadConfig();

      return (WANTS_OVERLAY_CALLBACK |
              WANTS_OPENGL_OVERLAY_CALLBACK |
              WANTS_TOOLBAR_CALLBACK    |
              WANTS_CONFIG              |
              WANTS_CURSOR_LATLON       |
              WANTS_NMEA_EVENTS         |
              WANTS_PLUGIN_MESSAGING
            );
}

bool weather_routing_pi::DeInit(void)
{
    m_tCursorLatLon.Stop();
    if(m_pWeather_Routing)
        m_pWeather_Routing->Close();
    WeatherRouting *wr = m_pWeather_Routing;
    m_pWeather_Routing = NULL; /* needed first as destructor may call event loop */
    delete wr;

    return true;
}

int weather_routing_pi::GetAPIVersionMajor()
{
      return MY_API_VERSION_MAJOR;
}

int weather_routing_pi::GetAPIVersionMinor()
{
      return MY_API_VERSION_MINOR;
}

int weather_routing_pi::GetPlugInVersionMajor()
{
      return PLUGIN_VERSION_MAJOR;
}

int weather_routing_pi::GetPlugInVersionMinor()
{
      return PLUGIN_VERSION_MINOR;
}

wxBitmap *weather_routing_pi::GetPlugInBitmap()
{
    return new wxBitmap(_img_WeatherRouting->ConvertToImage().Copy());
}

wxString weather_routing_pi::GetCommonName()
{
      return _("WeatherRouting");
}

wxString weather_routing_pi::GetShortDescription()
{
    return _("Compute optimal routes based on weather and constraints.");
}

wxString weather_routing_pi::GetLongDescription()
{
    return _("\
Weather Routing features include:\n\
  optimal routing subject to various constraints based on weather data\n\
  automatic boat polar computation\n\
");
}

void weather_routing_pi::SetDefaults(void)
{
}

int weather_routing_pi::GetToolbarToolCount(void)
{
      return 1;
}

void weather_routing_pi::SetCursorLatLon(double lat, double lon)
{
    if(m_pWeather_Routing && m_pWeather_Routing->FirstCurrentRouteMap() && !m_tCursorLatLon.IsRunning())
        m_tCursorLatLon.Start(50, true);

    m_cursor_lat = lat;
    m_cursor_lon = lon;
}

void weather_routing_pi::SetPluginMessage(wxString &message_id, wxString &message_body)
{
    if(message_id == _T("GRIB_TIMELINE"))
    {
        Json::Reader r;
        Json::Value v;
        r.parse(static_cast<std::string>(message_body), v);

        if (v["Day"].asInt() != -1) {
            wxDateTime time;
        
            time.Set
              (v["Day"].asInt(), (wxDateTime::Month)v["Month"].asInt(), v["Year"].asInt(),
               v["Hour"].asInt(), v["Minute"].asInt(), v["Second"].asInt());

            if (m_pWeather_Routing && time.IsValid()) {
                m_pWeather_Routing->m_ConfigurationDialog.m_GribTimelineTime = time.ToUTC();
//            m_pWeather_Routing->m_ConfigurationDialog.m_cbUseGrib->Enable();
                RequestRefresh(m_parent_window);
            }
        }
    }
    if(message_id == _T("GRIB_TIMELINE_RECORD"))
    {
        Json::Reader r;
        Json::Value v;
        r.parse(static_cast<std::string>(message_body), v);

        static bool shown_warnings;
        if(!shown_warnings) {
            shown_warnings = true;

            int grib_version_major = v["GribVersionMajor"].asInt();
            int grib_version_minor = v["GribVersionMinor"].asInt();

            int grib_version = 1000*grib_version_major + grib_version_minor;
            int grib_min =     1000*GRIB_MIN_MAJOR     + GRIB_MIN_MINOR;
            int grib_max =     1000*GRIB_MAX_MAJOR     + GRIB_MAX_MINOR;

            if(grib_version < grib_min || grib_version > grib_max) {
                wxMessageDialog mdlg(m_parent_window,
                                     _("Grib plugin version not supported.")
                                     + _T("\n\n") +
                                     wxString::Format(_("Use versions %d.%d to %d.%d"), GRIB_MIN_MAJOR, GRIB_MIN_MINOR, GRIB_MAX_MAJOR, GRIB_MAX_MINOR),
                                     _("Weather Routing"), wxOK | wxICON_WARNING);
                mdlg.ShowModal();
            }
        }

        wxString sptr = v["TimelineSetPtr"].asString();
        wxCharBuffer bptr = sptr.To8BitData();
        const char* ptr = bptr.data();

        GribRecordSet *gptr;
        sscanf(ptr, "%p", &gptr);

        if(m_pWeather_Routing) {
            RouteMapOverlay *routemapoverlay = m_pWeather_Routing->m_RouteMapOverlayNeedingGrib;
            if(routemapoverlay) {
                routemapoverlay->Lock();
                routemapoverlay->SetNewGrib(gptr);
                routemapoverlay->Unlock();
            }
        }
    }
    if(message_id == _T("CLIMATOLOGY"))
    {
        if(!m_pWeather_Routing)
            return; /* not ready */

        Json::Reader r;
        Json::Value v;
        r.parse(static_cast<std::string>(message_body), v);

        static bool shown_warnings;
        if(!shown_warnings) {
            shown_warnings = true;

            int climatology_version_major = v["ClimatologyVersionMajor"].asInt();
            int climatology_version_minor = v["ClimatologyVersionMinor"].asInt();

            int climatology_version = 1000*climatology_version_major + climatology_version_minor;
            int climatology_min =     1000*CLIMATOLOGY_MIN_MAJOR     + CLIMATOLOGY_MIN_MINOR;
            int climatology_max =     1000*CLIMATOLOGY_MAX_MAJOR     + CLIMATOLOGY_MAX_MINOR;

            if(climatology_version < climatology_min || climatology_version > climatology_max) {
                wxMessageDialog mdlg(m_parent_window,
                                     _("Climatology plugin version not supported, no climatology data.")
                                     + _T("\n\n") +
                                     wxString::Format(_("Use versions %d.%d to %d.%d"), CLIMATOLOGY_MIN_MAJOR, CLIMATOLOGY_MIN_MINOR, CLIMATOLOGY_MAX_MAJOR, CLIMATOLOGY_MAX_MINOR),
                                     _("Weather Routing"), wxOK | wxICON_WARNING);
                mdlg.ShowModal();
                return;
            }
        }

        wxString sptr = v["ClimatologyDataPtr"].asString();
        sscanf(sptr.To8BitData().data(), "%p", &RouteMap::ClimatologyData);

        sptr = v["ClimatologyWindAtlasDataPtr"].asString();
        sscanf(sptr.To8BitData().data(), "%p", &RouteMap::ClimatologyWindAtlasData);

        sptr = v["ClimatologyCycloneTrackCrossingsPtr"].asString();
        sscanf(sptr.To8BitData().data(), "%p", &RouteMap::ClimatologyCycloneTrackCrossings);

        if(m_pWeather_Routing) {
            m_pWeather_Routing->m_ConfigurationDialog.m_cClimatologyType->Enable
                (RouteMap::ClimatologyData!=NULL);
            m_pWeather_Routing->m_ConfigurationDialog.m_cbAvoidCycloneTracks->Enable
                (RouteMap::ClimatologyCycloneTrackCrossings!=NULL);
        }
    }


    if(message_id == wxS("WEATHER_ROUTING_PI")) {
        // now read the JSON text and store it in the 'root' structure
        Json::Value  root;
        Json::Reader  reader;
        // check for errors before retreiving values...
        if (!reader.parse( static_cast<std::string>(message_body), root )) {
            wxLogMessage(_T("weather_routing_pi: Error parsing JSON message - ") 
                 +reader.getFormattedErrorMessages() + " : " + message_body );
        }
        
        if(root["Type"].asString() == "Response" && root["Source"].asString() == "OCPN_DRAW_PI") {
            if(root["Msg"].asString() == "Version" ) {
                if(root["MsgId"].asString() == "version")
                    g_ReceivedODVersionJSONMsg = root;
            } else
            if(root["Msg"].asString() == "GetAPIAddresses" ) {
                wxString sptr = root["OD_FindClosestBoundaryLineCrossing"].asString();
                sscanf(sptr.To8BitData().data(), "%p", &RouteMap::ODFindClosestBoundaryLineCrossing);
            }
            else if (root["Msg"].asString() == "FindPointInAnyBoundary" ) {
              if (root["MsgId"].asString() == "exist") {
                 b_in_boundary_reply = root["Found"].asBool() == true;
                 // if (b_in_boundary_reply) printf("collision with %s\n", (const char*)root[wxS("GUID")].AsString().mb_str());
              }
            }
        }
    }
}


// true if lat lon in any active boundary, aka we can't exit it.
// use JSON msg rather than binary it's not time sensitive.
bool weather_routing_pi::InBoundary(double lat, double lon)
{
    Json::Value jMsg;
    Json::FastWriter writer;

    jMsg["Source"] = "WEATHER_ROUTING_PI";
    jMsg["Type"] = "Request";

    jMsg["Msg"] = "FindPointInAnyBoundary";
    jMsg["MsgId"] = "exist";

    jMsg["lat"] = lat;
    jMsg["lon"] = lon;

    jMsg["BoundaryState"] = "Active";
    jMsg["BoundaryType"] = "Exclusion";

    b_in_boundary_reply = false;
    SendPluginMessage( "OCPN_DRAW_PI", writer.write( jMsg ) );

    return b_in_boundary_reply;
}

void weather_routing_pi::SetPositionFixEx(PlugIn_Position_Fix_Ex &pfix)
{
    m_boat_lat = pfix.Lat;
    m_boat_lon = pfix.Lon;
}

void weather_routing_pi::ShowPreferencesDialog( wxWindow* parent )
{
}

void weather_routing_pi::OnToolbarToolCallback(int id)
{
    if(!m_pWeather_Routing) {
        m_pWeather_Routing = new WeatherRouting(m_parent_window, *this);
        wxPoint p = m_pWeather_Routing->GetPosition();
        m_pWeather_Routing->Move(0,0);        // workaround for gtk autocentre dialog behavior
        m_pWeather_Routing->Move(p);

        SendPluginMessage(wxString(_T("GRIB_TIMELINE_REQUEST")), _T(""));
        SendPluginMessage(wxString(_T("CLIMATOLOGY_REQUEST")), _T(""));

        if(ODVersionNewerThan( 1, 1, 15)) {
            Json::Value jMsg;
            Json::FastWriter writer;

            jMsg["Source"] = "WEATHER_ROUTING_PI";
            jMsg["Type"] = "Request";
            jMsg["Msg"] = "GetAPIAddresses";
            jMsg["MsgId"] = "GetAPIAddresses";
            SendPluginMessage( wxS("OCPN_DRAW_PI"), writer.write( jMsg) );
        }
        
        m_pWeather_Routing->Reset();
    }

    m_pWeather_Routing->Show(!m_pWeather_Routing->IsShown());
}

void weather_routing_pi::OnContextMenuItemCallback(int id)
{
    if(!m_pWeather_Routing)
        return;

    if(id == m_position_menu_id) {
        m_pWeather_Routing->AddPosition(m_cursor_lat, m_cursor_lon);
    }
    else if(id == m_waypoint_menu_id) {
        wxString GUID = GetSelectedWaypointGUID_Plugin();
        if (GUID.IsEmpty())
          return;
        std::unique_ptr<PlugIn_Waypoint> w = GetWaypoint_Plugin( GUID);
        PlugIn_Waypoint *wp = w.get();
        if (wp == nullptr)
            return;
        m_pWeather_Routing->AddPosition(wp->m_lat, wp->m_lon, wp->m_MarkName, wp->m_GUID);
    }
    m_pWeather_Routing->Reset();
}

bool weather_routing_pi::RenderOverlay(wxDC &wxdc, PlugIn_ViewPort *vp)
{
    if(m_pWeather_Routing && m_pWeather_Routing->IsShown()) {
        piDC dc(wxdc);
        m_pWeather_Routing->Render(dc, *vp);
        return true;
    }
    return false;
}

bool weather_routing_pi::RenderGLOverlay(wxGLContext *pcontext, PlugIn_ViewPort *vp)
{    
    if(m_pWeather_Routing && m_pWeather_Routing->IsShown()) {
        piDC dc;
        dc.SetVP(vp);
        m_pWeather_Routing->Render(dc, *vp);
        return true;
    }
    return false;
}

void weather_routing_pi::OnCursorLatLonTimer( wxTimerEvent & )
{
    if (m_pWeather_Routing == 0)
        return;

    std::list<RouteMapOverlay *>routemapoverlays = m_pWeather_Routing->CurrentRouteMaps();
    bool refresh = false;
    for(std::list<RouteMapOverlay *>::iterator it = routemapoverlays.begin();
        it != routemapoverlays.end(); it++)
        if((*it)->SetCursorLatLon(m_cursor_lat, m_cursor_lon))
            refresh = true;

    m_pWeather_Routing->UpdateCursorPositionDialog();
    m_pWeather_Routing->UpdateRoutePositionDialog();

    if(refresh) {
        RequestRefresh(m_parent_window);
        m_pWeather_Routing->CursorRouteChanged();
    }
}

bool weather_routing_pi::LoadConfig(void)
{
      wxFileConfig *pConf = (wxFileConfig *)m_pconfig;

      if(!pConf)
          return false;

      pConf->SetPath ( _T( "/PlugIns/WeatherRouting" ) );
      return true;
}

bool weather_routing_pi::SaveConfig(void)
{
      wxFileConfig *pConf = (wxFileConfig *)m_pconfig;

      if(!pConf)
          return false;

      pConf->SetPath ( _T ( "/PlugIns/WeatherRouting" ) );
      return true;
}

void weather_routing_pi::SetColorScheme(PI_ColorScheme cs)
{
      DimeWindow(m_pWeather_Routing);
}

wxString weather_routing_pi::StandardPath()
{
    wxString s = wxFileName::GetPathSeparator();
    wxString stdPath  = *GetpPrivateApplicationDataLocation();

    stdPath += s + _T("plugins");
    if (!wxDirExists(stdPath))
      wxMkdir(stdPath);

    stdPath += s + _T("weather_routing");

#ifdef __WXOSX__
    // Compatibility with pre-OCPN-4.2; move config dir to
    // ~/Library/Preferences/opencpn if it exists
    {
        wxStandardPathsBase& std_path = wxStandardPathsBase::Get();
        wxString s = wxFileName::GetPathSeparator();
        // should be ~/Library/Preferences/opencpn
        wxString oldPath = (std_path.GetUserConfigDir() +s + _T("plugins") +s + _T("weather_routing"));
        if (wxDirExists(oldPath) && !wxDirExists(stdPath)) {
		    wxLogMessage("weather_routing_pi: moving config dir %s to %s", oldPath, stdPath);
		    wxRenameFile(oldPath, stdPath);
        }
    }
#endif

    if (!wxDirExists(stdPath))
      wxMkdir(stdPath);

    stdPath += s;
    return stdPath;
}

void weather_routing_pi::ShowMenuItems(bool show)
{
    SetToolbarItemState( m_leftclick_tool_id, show );
    SetCanvasMenuItemViz(m_position_menu_id, show);
    SetCanvasMenuItemViz(m_waypoint_menu_id, show, "Waypoint");
}
