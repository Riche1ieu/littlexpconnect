/*****************************************************************************
* Copyright 2015-2017 Alexander Barthel albar965@mailbox.org
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#include "xpconnect.h"
#include "dataref.h"

extern "C" {
#include "XPLMPlugin.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMProcessing.h"
#include "XPLMDataAccess.h"
#include "XPLMMenus.h"
#include "XPLMUtilities.h"
#include "XPWidgets.h"
#include "XPStandardWidgets.h"
#include "XPLMScenery.h"
}

#include "fs/sc/xpconnecthandler.h"
#include "fs/sc/simconnectdata.h"
#include "fs/sc/simconnectreply.h"
#include "fs/sc/simconnecttypes.h"
#include "gui/consoleapplication.h"
#include "fs/util/fsutil.h"
#include "fs/ns/navserver.h"
#include "fs/sc/connecthandler.h"
#include "fs/sc/datareaderthread.h"
#include "geo/calculations.h"
#include "fs/sc/xpconnecthandler.h"
#include "settings/settings.h"

#include <QThread>

using atools::geo::kgToLbs;
using atools::settings::Settings;

namespace xpc {

XpConnect *XpConnect::object = nullptr;

/* key names for atools::settings */
static const QLatin1Literal SETTINGS_OPTIONS_DEFAULT_PORT("Options/DefaultPort");
static const QLatin1Literal SETTINGS_OPTIONS_UPDATE_RATE_MS("Options/UpdateRate");
static const QLatin1Literal SETTINGS_OPTIONS_FETCH_RATE_MS("Options/FetchRate");
static const QLatin1Literal SETTINGS_OPTIONS_FETCH_AI_AIRCRAFT("Options/FetchAiAircraft");
static const QLatin1Literal SETTINGS_OPTIONS_RECONNECT_RATE_SEC("Options/ReconnectRate");
static const QLatin1Literal SETTINGS_OPTIONS_FETCH_AI_SHIP("Options/FetchAiShip");

// DataRefs ======================================================
namespace dr {
// Contains all datarefs for simple initialization
static DataRefPtrVector dataRefs;

static DataRef simBuild(dataRefs, "sim/version/sim_build_string");
static DataRef xplmBuild(dataRefs, "sim/version/xplm_build_string");
static DataRef simPaused(dataRefs, "sim/time/paused");

// SimConnectUserAircraft
static DataRef windSpeedKts(dataRefs, "sim/cockpit2/gauges/indicators/wind_speed_kts");
static DataRef windDirectionDegMag(dataRefs, "sim/cockpit2/gauges/indicators/wind_heading_deg_mag");

// Temperatures
// Static air temperature (SAT) is also called: outside air temperature (OAT) or true air temperature
// Lower than TAT
static DataRef ambientTemperatureCelsius(dataRefs, "sim/weather/temperature_ambient_c");

// Total air temperature (TAT) is also called: indicated air temperature (IAT) or ram air temperature (RAT)
// higher than SAT
static DataRef totalAirTemperatureCelsius(dataRefs, "sim/weather/temperature_le_c");

static DataRef seaLevelPressureMbar(dataRefs, "sim/physics/earth_pressure_p");

// Ice
static DataRef pitotIcePercent(dataRefs, "sim/flightmodel/failures/pitot_ice");
static DataRef structuralIcePercent(dataRefs, "sim/flightmodel/failures/frm_ice");
static DataRef structuralIcePercent2(dataRefs, "sim/flightmodel/failures/frm_ice2");

// Weight
static DataRef airplaneTotalWeightKgs(dataRefs, "sim/flightmodel/weight/m_total");
static DataRef airplaneMaxGrossWeightKgs(dataRefs, "sim/aircraft/weight/acf_m_max");
static DataRef airplaneEmptyWeightKgs(dataRefs, "sim/aircraft/weight/acf_m_empty");
static DataRef airplanePayloadWeightKgs(dataRefs, "sim/flightmodel/weight/m_fixed");

// static DataRef fuelTotalQuantityGallons(dataRefs, ""); value calculated
static DataRef fuelTotalWeightKgs(dataRefs, "sim/flightmodel/weight/m_fuel_total");

static DataRef fuelFlowKgSec8(dataRefs, "sim/cockpit2/engine/indicators/fuel_flow_kg_sec");
// static DataRef fuelFlowGPH(dataRefs, ""); value calculated

static DataRef magVarDeg(dataRefs, "sim/flightmodel/position/magnetic_variation");
static DataRef ambientVisibilityMeter(dataRefs, "sim/weather/visibility_reported_m");
static DataRef trackMagDeg(dataRefs, "sim/cockpit2/gauges/indicators/ground_track_mag_pilot");
// static DataRef trackTrueDeg(dataRefs, ""); value calculated

// Date and time
static DataRef localDateDays(dataRefs, "sim/time/local_date_days");
static DataRef localTimeSec(dataRefs, "sim/time/local_time_sec");
static DataRef zuluTimeSec(dataRefs, "sim/time/zulu_time_sec");

// SimConnectAircraft
static DataRef airplaneReg(dataRefs, "sim/aircraft/view/acf_tailnum");
static DataRef airplaneTitle(dataRefs, "sim/aircraft/view/acf_descrip");
static DataRef airplaneType(dataRefs, "sim/aircraft/view/acf_ICAO");

// Position and more
static DataRef latPositionDeg(dataRefs, "sim/flightmodel/position/latitude");
static DataRef lonPositionDeg(dataRefs, "sim/flightmodel/position/longitude");
static DataRef indicatedSpeedKts(dataRefs, "sim/flightmodel/position/indicated_airspeed");
static DataRef trueSpeedKts(dataRefs, "sim/flightmodel/position/true_airspeed");
static DataRef groundSpeedKts(dataRefs, "sim/flightmodel/position/groundspeed");

static DataRef indicatedAltitudeFt(dataRefs, "sim/cockpit2/gauges/indicators/altitude_ft_pilot");
static DataRef actualAltitudeMeter(dataRefs, "sim/flightmodel/position/elevation");
static DataRef aglAltitudeFt(dataRefs, "sim/cockpit2/gauges/indicators/radio_altimeter_height_ft_pilot");

static DataRef headingTrueDeg(dataRefs, "sim/flightmodel/position/true_psi");
static DataRef headingMagDeg(dataRefs, "sim/flightmodel/position/mag_psi");
static DataRef machSpeed(dataRefs, "sim/flightmodel/misc/machno");
static DataRef verticalSpeedFeetPerMin(dataRefs, "sim/flightmodel/position/vh_ind_fpm");
static DataRef numberOfEngines(dataRefs, "sim/aircraft/engine/acf_num_engines");

static DataRef onGround(dataRefs, "sim/flightmodel/failures/onground_any");
static DataRef rainPercentage(dataRefs, "sim/weather/rain_percent");

static DataRef engineType8(dataRefs, "sim/aircraft/prop/acf_en_type");
// 0 = recip carb, 1 = recip injected, 2 = free turbine, 3 = electric, 4 = lo bypass jet, 5 = hi bypass jet, 6 = rocket, 7 = tip rockets, 8 = fixed turbine
enum XpEngineType
{
  RECIP_CARB = 0,
  RECIP_INJECTED = 1,
  FREE_TURBINE = 2,
  ELECTRIC = 3,
  LO_BYPASS_JET = 4,
  HI_BYPASS_JET = 5,
  ROCKET = 6,
  TIP_ROCKETS = 7,
  FIXED_TURBINE = 8
};

}

// ====================================================================================
// ServerThread
// ====================================================================================

/*
 * Main thread running everything else. Contains the event loop.
 */
class ServerThread :
  public QThread
{
public:
  ServerThread()
  {

  }

  virtual ~ServerThread();

private:
  virtual void run() override;

};

ServerThread::~ServerThread()
{

}

void ServerThread::run()
{
  // Create TCP server and threads
  XpConnect::instance().createNavServer();

  // Start event loop
  int result = exec();

  qInfo() << Q_FUNC_INFO << "Event loop exited with result" << result;

  // Shutdown all
  XpConnect::instance().deleteNavServer();
}

// ====================================================================================
// XpConnect
// ====================================================================================

void XpConnect::createServerThread()
{
  deleteServerThread();
  serverThread = new ServerThread();
  serverThread->start();
}

void XpConnect::deleteServerThread()
{
  if(serverThread != nullptr)
  {
    serverThread->quit();
    serverThread->wait();
    delete serverThread;
    serverThread = nullptr;
  }
}

XpConnect::XpConnect()
{

}

XpConnect::~XpConnect()
{
  deleteServerThread();
}

XpConnect& XpConnect::instance()
{
  if(object == nullptr)
    object = new XpConnect();
  return *object;
}

void XpConnect::shutdown()
{
  delete object;
  object = nullptr;
}

void XpConnect::pluginEnable()
{
  initDataRefs();

  qInfo() << "Simulator build" << dr::simBuild.valueString() << "XPLM build" << dr::xplmBuild.valueString();

  // Start main server thread which will spawn everything else
  deleteServerThread();
  serverThread = new ServerThread();
  serverThread->start();
  qDebug() << Q_FUNC_INFO << "Server started";
}

void XpConnect::pluginDisable()
{
  deleteServerThread();
  qDebug() << Q_FUNC_INFO << "Server deleted";
}

float XpConnect::flightLoopCallback(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter)
{
  // All datarefs are read on the X-Plane main plugin thread
  Q_UNUSED(inElapsedSinceLastCall);
  Q_UNUSED(inElapsedTimeSinceLastFlightLoop);
  Q_UNUSED(inCounter);
  // qDebug() << "LittleXpConnect" << Q_FUNC_INFO << "inElapsedSinceLastCall" << inElapsedSinceLastCall
  // << "inElapsedTimeSinceLastFlightLoop" << inElapsedTimeSinceLastFlightLoop
  // << "inCounter" << inCounter;

  {
    // Copy into a temporary object first to make the sync area smaller and get this quickly done
    atools::fs::sc::SimConnectData data;
    atools::fs::sc::SimConnectUserAircraft& userAircraft = data.userAircraft;

    userAircraft.magVarDeg = dr::magVarDeg.valueFloat();

    // Wind and ambient parameters
    userAircraft.windSpeedKts = dr::windSpeedKts.valueFloat();
    userAircraft.windDirectionDegT = dr::windDirectionDegMag.valueFloat() - userAircraft.magVarDeg;
    userAircraft.ambientTemperatureCelsius = dr::ambientTemperatureCelsius.valueFloat();
    userAircraft.totalAirTemperatureCelsius = dr::totalAirTemperatureCelsius.valueFloat();
    userAircraft.seaLevelPressureMbar = dr::seaLevelPressureMbar.valueFloat() / 100.f;

    // Ice
    userAircraft.pitotIcePercent = dr::pitotIcePercent.valueFloat() * 100.f;
    userAircraft.structuralIcePercent = (dr::structuralIcePercent.valueFloat() +
                                         dr::structuralIcePercent2.valueFloat()) / 2.f * 100.f;

    // Weight
    userAircraft.airplaneTotalWeightLbs = kgToLbs(dr::airplaneTotalWeightKgs.valueFloat());
    userAircraft.airplaneMaxGrossWeightLbs = kgToLbs(dr::airplaneMaxGrossWeightKgs.valueFloat());
    userAircraft.airplaneEmptyWeightLbs = kgToLbs(dr::airplaneEmptyWeightKgs.valueFloat());

    // Fuel flow in weight
    userAircraft.fuelTotalWeightLbs = kgToLbs(dr::fuelTotalWeightKgs.valueFloat());
    userAircraft.fuelFlowPPH = kgToLbs(dr::fuelFlowKgSec8.valueFloatArrSum()) * 3600.f;

    userAircraft.ambientVisibilityMeter = dr::ambientVisibilityMeter.valueFloat();

    // Build local time and use timezone offset from simulator
    // X-Plane does not allow to set the year
    QDate localDate = QDate::currentDate();
    localDate.setDate(localDate.year(), 1, 1);
    localDate = localDate.addDays(dr::localDateDays.valueInt());
    int localTimeSec = static_cast<int>(dr::localTimeSec.valueFloat());
    int zuluTimeSec = static_cast<int>(dr::zuluTimeSec.valueFloat());
    int offsetSeconds = zuluTimeSec - localTimeSec;

    QTime localTime = QTime::fromMSecsSinceStartOfDay(localTimeSec * 1000);
    QDateTime localDateTime(localDate, localTime, Qt::OffsetFromUTC, -offsetSeconds);
    userAircraft.localDateTime = localDateTime;

    QTime zuluTime = QTime::fromMSecsSinceStartOfDay(zuluTimeSec * 1000);
    userAircraft.zuluDateTime = QDateTime(localDate, zuluTime, Qt::UTC);

    // SimConnectAircraft
    userAircraft.airplaneTitle = dr::airplaneTitle.valueString();
    userAircraft.airplaneModel = dr::airplaneType.valueString();
    userAircraft.airplaneReg = dr::airplaneReg.valueString();
    // userAircraft.airplaneType;           // not available - use model ICAO code in client
    // userAircraft.airplaneAirline;        // not available
    // userAircraft.airplaneFlightnumber;   // not available
    // userAircraft.fromIdent;              // not available
    // userAircraft.toIdent;                // not available

    float actualAlt = atools::geo::meterToFeet(dr::actualAltitudeMeter.valueFloat());
    userAircraft.position =
      atools::geo::Pos(dr::lonPositionDeg.valueFloat(), dr::latPositionDeg.valueFloat(), actualAlt);

    userAircraft.altitudeAboveGroundFt = dr::aglAltitudeFt.valueFloat();
    userAircraft.groundAltitudeFt = actualAlt - userAircraft.altitudeAboveGroundFt;
    userAircraft.indicatedAltitudeFt = dr::indicatedAltitudeFt.valueFloat();

    // Heading and track
    userAircraft.headingMagDeg = dr::headingMagDeg.valueFloat();
    userAircraft.headingTrueDeg = dr::headingTrueDeg.valueFloat();
    userAircraft.trackMagDeg = dr::trackMagDeg.valueFloat();
    userAircraft.trackTrueDeg = userAircraft.trackMagDeg - userAircraft.magVarDeg;

    // Speed
    userAircraft.indicatedSpeedKts = dr::indicatedSpeedKts.valueFloat();
    userAircraft.trueSpeedKts = dr::trueSpeedKts.valueFloat();
    userAircraft.machSpeed = dr::machSpeed.valueFloat();
    userAircraft.verticalSpeedFeetPerMin = dr::verticalSpeedFeetPerMin.valueFloat();
    userAircraft.groundSpeedKts = dr::groundSpeedKts.valueFloat();

    // Model
    userAircraft.modelRadiusFt = 0; // not available - disable display in client which will use a default value
    userAircraft.wingSpanFt = 0; // not available- disable display in client

    // Set misc flags
    userAircraft.flags = atools::fs::sc::IS_USER;
    if(dr::onGround.valueInt() > 0)
      userAircraft.flags |= atools::fs::sc::ON_GROUND;
    if(dr::rainPercentage.valueFloat() > 0.1f)
      userAircraft.flags |= atools::fs::sc::IN_RAIN;
    // IN_CLOUD = 0x0002, - not available
    // IN_SNOW = 0x0008,  - not available

    // Category is not available - Disable display of category on client
    userAircraft.category = atools::fs::sc::UNKNOWN;
    // AIRPLANE, HELICOPTER, BOAT, GROUNDVEHICLE, CONTROLTOWER, SIMPLEOBJECT, VIEWER

    // Value to calculate fuel volume from mass
    float fuelMassToVolDivider = 6.f;

    // Get the engine array
    IntVector engines = dr::engineType8.valueIntArr();
    userAircraft.engineType = atools::fs::sc::UNSUPPORTED;
    // PISTON = 0, JET = 1, NO_ENGINE = 2, HELO_TURBINE = 3, UNSUPPORTED = 4, TURBOPROP = 5

    // Get engine type
    for(int engine : engines)
    {
      dr::XpEngineType type = static_cast<dr::XpEngineType>(engine);
      switch(type)
      {
        case xpc::dr::ELECTRIC:
        case xpc::dr::RECIP_CARB:
        case xpc::dr::RECIP_INJECTED:
          userAircraft.engineType = atools::fs::sc::PISTON;
          fuelMassToVolDivider = 6.f; // Avgas lbs to gallons at standard temp
          break;

        case xpc::dr::FREE_TURBINE:
        case xpc::dr::FIXED_TURBINE:
          fuelMassToVolDivider = 6.7f; // JetA lbs to gallons at standard temp
          userAircraft.engineType = atools::fs::sc::TURBOPROP;
          break;

        case xpc::dr::ROCKET:
        case xpc::dr::TIP_ROCKETS:
        case xpc::dr::LO_BYPASS_JET:
        case xpc::dr::HI_BYPASS_JET:
          fuelMassToVolDivider = 6.7f; // JetA lbs to gallons at standard temp
          userAircraft.engineType = atools::fs::sc::JET;
          break;
      }
      if(userAircraft.engineType != atools::fs::sc::UNSUPPORTED)
        // Found one - stop here
        break;
    }

    // Calculate fuell volume based on type
    userAircraft.fuelTotalQuantityGallons = userAircraft.fuelTotalWeightLbs / fuelMassToVolDivider;
    userAircraft.fuelFlowGPH = userAircraft.fuelFlowPPH / fuelMassToVolDivider;

    userAircraft.numberOfEngines = static_cast<quint8>(dr::numberOfEngines.valueInt());

    {
      // Syncronize since DataReaderThread accesses these too through the callback
      QMutexLocker locker(&currentDataMutex);
      currentData = data;
    }
  }

  return fetchRateSecs;
}

void XpConnect::initDataRefs()
{
  for(DataRef *ref : dr::dataRefs)
  {
    if(!ref->isValid())
      ref->find();
  }
}

void XpConnect::receiveMessage(XPLMPluginID inFromWho, long inMessage, void *inParam)
{
  Q_UNUSED(inParam);
  qDebug() << "LittleXpConnect" << Q_FUNC_INFO << "inFromWho" << inFromWho << "inMessage" << inMessage;

  if(inFromWho == XPLM_PLUGIN_XPLANE)
  {
    switch(inMessage)
    {
      case XPLM_MSG_PLANE_CRASHED:
        qDebug() << "XPLM_MSG_PLANE_CRASHED";
        break;
      case XPLM_MSG_PLANE_LOADED:
        qDebug() << "XPLM_MSG_PLANE_LOADED";
        break;
      case XPLM_MSG_AIRPORT_LOADED:
        qDebug() << "XPLM_MSG_AIRPORT_LOADED";
        break;
      case XPLM_MSG_SCENERY_LOADED:
        qDebug() << "XPLM_MSG_SCENERY_LOADED";
        break;
      case XPLM_MSG_AIRPLANE_COUNT_CHANGED:
        qDebug() << "XPLM_MSG_AIRPLANE_COUNT_CHANGED";
        break;
      case XPLM_MSG_PLANE_UNLOADED:
        qDebug() << "XPLM_MSG_PLANE_UNLOADED";
        break;
    }
  }
}

bool XpConnect::copyData(atools::fs::sc::SimConnectData& data, int radiusKm, atools::fs::sc::Options options)
{
  Q_UNUSED(radiusKm);
  Q_UNUSED(options);

  {
    QMutexLocker locker(&currentDataMutex);
    if(currentData.isUserAircraftValid())
    {
      data = currentData;
      currentData = atools::fs::sc::EMPTY_SIMCONNECT_DATA;
    }
    else
      return false;
  }
  return true;
}

void XpConnect::createNavServer()
{
  qDebug() << "LittleXpConnect" << Q_FUNC_INFO;

  // Bind the callback to the copyData methods
  using namespace std::placeholders;
  atools::fs::sc::DataCopyFunctionType dataCopyFunc = std::bind(&XpConnect::copyData, this, _1, _2, _3);

  // Create the handler which will be used by the DataReaderThread to fetch the data
  bool verbose = false;
  atools::fs::sc::XpConnectHandler *xpHandler = new atools::fs::sc::XpConnectHandler(dataCopyFunc, verbose);
  connectHandler = xpHandler;

  dataReader = new atools::fs::sc::DataReaderThread(nullptr, connectHandler, verbose);

  Settings& settings = Settings::instance();

  dataReader->setReconnectRateSec(settings.getAndStoreValue(xpc::SETTINGS_OPTIONS_RECONNECT_RATE_SEC, 10).toInt());
  // Update rate that is used to send out network packets
  dataReader->setUpdateRate(settings.getAndStoreValue(xpc::SETTINGS_OPTIONS_UPDATE_RATE_MS, 500).toUInt());

  fetchRateSecs = settings.getAndStoreValue(xpc::SETTINGS_OPTIONS_FETCH_RATE_MS, 200).toFloat() / 1000.f;

  atools::fs::sc::Options options = atools::fs::sc::NO_OPTION;
  if(settings.getAndStoreValue(xpc::SETTINGS_OPTIONS_FETCH_AI_AIRCRAFT, true).toBool())
    options |= atools::fs::sc::FETCH_AI_AIRCRAFT;
  if(settings.getAndStoreValue(xpc::SETTINGS_OPTIONS_FETCH_AI_SHIP, true).toBool())
    options |= atools::fs::sc::FETCH_AI_BOAT;
  dataReader->setSimconnectOptions(options);

  // Create nav TCP server but do not start it yet
  int defaultPort = settings.getAndStoreValue(xpc::SETTINGS_OPTIONS_DEFAULT_PORT, 51968).toInt();
  navServer = new atools::fs::ns::NavServer(nullptr, atools::fs::ns::NO_HTML, defaultPort);

  dataReader->start();
  navServer->startServer(dataReader);
}

void XpConnect::deleteNavServer()
{
  if(navServer != nullptr)
    navServer->stopServer();
  qDebug() << Q_FUNC_INFO << "navServer stopped";

  delete navServer;
  qDebug() << Q_FUNC_INFO << "navServer deleted";

  if(dataReader != nullptr)
    dataReader->terminateThread();
  qDebug() << Q_FUNC_INFO << "dataReader terminated";

  delete dataReader;
  qDebug() << Q_FUNC_INFO << "dataReader deleted";

  delete connectHandler;
  qDebug() << Q_FUNC_INFO << "connectHandler deleted";
}

} // namespace xpc
