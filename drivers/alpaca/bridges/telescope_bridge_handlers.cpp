/*******************************************************************************
  Copyright(c) 2025 Jasem Mutlaq. All rights reserved.

  INDI Alpaca Telescope Bridge - Handlers Implementation

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the Free
  Software Foundation; either version 2 of the License, or (at your option)
  any later version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU Library General Public License
  along with this library; see the file COPYING.LIB.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301, USA.

  The full GNU General Public License is included in this distribution in the
  file called LICENSE.
*******************************************************************************/

#include "telescope_bridge.h"
#include "indilogger.h"
#include "device_manager.h"

#include <httplib.h>
#include <cmath>
#include <cstring>
#include <sstream>
#include <chrono>


#ifdef _USE_SYSTEM_JSONLIB
#include <nlohmann/json.hpp>
#else
#include <indijson.hpp>
#endif

using json = nlohmann::json;


/**
 * Compute Local Sidereal Time (LST) in hours [0, 24)
 *
 * @param longitude_deg  Observer's longitude in degrees (East positive, West negative)
 * @param tp             UTC time point (defaults to now)
 * @return               LST in decimal hours [0, 24)
 */
double TelescopeBridge::computeLST(double longitude_deg,
                                   std::chrono::system_clock::time_point tp = std::chrono::system_clock::now() )
{

    // Convert time_point to Julian Date
    // Unix epoch (1970-01-01 00:00:00 UTC) = JD 2440587.5
    using namespace std::chrono;
    double unix_seconds = duration<double>(tp.time_since_epoch()).count();
    double JD = 2440587.5 + unix_seconds / 86400.0;

    // Julian centuries from J2000.0 (JD 2451545.0)
    // double T = (JD - 2451545.0) / 36525.0;
    double JD0 = std::floor(JD - 0.5) + 0.5;  // JD at previous midnight
    double T = (JD0 - 2451545.0) / 36525.0;   // T at 0h UT

    // Greenwich Mean Sidereal Time (GMST) in seconds at 0h UT
    // IAU formula (Astronomical Almanac)
    double GMST_seconds = 24110.54841
                        + 8640184.812866 * T
                        + 0.093104      * T * T
                        - 6.2e-6        * T * T * T;

    // Add UT contribution: Earth rotates 360° in one sidereal day
    // 1 solar day = 1.00273790935 sidereal days
    //double UT_seconds = std::fmod(unix_seconds, 86400.0);
    double UT_seconds  = (JD - JD0) * 86400.0;         // seconds since midnight
    GMST_seconds += UT_seconds * 1.00273790935;

    // Convert to hours and normalize to [0, 24)
    double GMST_hours = std::fmod(GMST_seconds / 3600.0, 24.0);
    if (GMST_hours < 0.0) GMST_hours += 24.0;

    // Apply longitude offset (15°/hour)
    double LST = GMST_hours + longitude_deg / 15.0;

    // Normalize to [0, 24)
    LST = std::fmod(LST, 24.0);
    if (LST < 0.0) LST += 24.0;

    return LST;
}

// Common Alpaca API methods
void TelescopeBridge::handleConnected(const httplib::Request &req, httplib::Response &res)
{
    bool isConnected = m_Device.isConnected();
    json response;

    // Handle PUT request to connect/disconnect
    if (req.method == "PUT")
    {
        try
        {
            // Parse form-urlencoded data from request body
            std::map<std::string, std::string> formData;
            DeviceManager::parseFormUrlEncodedBody(req.body, formData);

            // Check if Connected parameter exists
            if (formData.find("Connected") == formData.end())
            {
                sendResponseValue(res, isConnected, false, "Missing Connected parameter");
                return;
            }

            // Convert Connected value to bool
            bool connected = (formData["Connected"] == "True" || formData["Connected"] == "true" ||
                              formData["Connected"] == "1");

            if (connected && !isConnected)
            {
                // Connect the device
                auto connectSw = m_Device.getSwitch("CONNECTION");
                if (connectSw)
                {
                    INDI::PropertySwitch switchProperty(connectSw);
                    switchProperty.reset();
                    switchProperty[0].setState(ISS_ON);
                    // Request to send the switch to the device
                    requestNewSwitch(switchProperty);
                    sendResponseValue(res, true);
                }
                else
                {
                    sendResponseValue(res, false, false, "Failed to connect device");
                }
            }
            else if (!connected && isConnected)
            {
                // DO NOT DISCONNECT DEVICE FROM INDI
                // JUST CONSIDER THAT THIS IS THE BEHAVIOUR
                // WITH TWO DISTINCT INDI CLIENTS, FOR EXAMPLE TWO CCDCIEL INSTANCES
                // SO LET'S BEHAVE ALPACA DISCONNECT IN A SIMILAR WAY TO NOT BREAK OTHER ALPACA OR INDI CLIENTS
                //  
                // Return value is false, because Alpaca requires it after Alpaca disconnection
                sendResponseValue(res,  false);

                // // Disconnect the device
                // auto connectSw = m_Device.getSwitch("CONNECTION");
                // if (connectSw)
                // {
                //     INDI::PropertySwitch switchProperty(connectSw);
                //     switchProperty.reset();
                //     switchProperty[1].setState(ISS_ON);
                //     // Request to send the switch to the device
                //     requestNewSwitch(switchProperty);
                //     sendResponseValue(res, false);
                // }
                // else
                // {
                //     sendResponseValue(res, true, false, "Failed to disconnect device");
                // }
            }
            else
            {
                // No change needed
                sendResponseValue(res, isConnected);
            }
        }
        catch (const std::exception &e)
        {
            sendResponseValue(res, isConnected, false, std::string("Invalid request: ") + e.what());
        }
    }
    else
    {
        // GET request
        sendResponseValue(res, isConnected);
    }
}

void TelescopeBridge::handleName(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    std::string deviceName = m_Device.getDeviceName();
    sendResponseValue(res, deviceName);
}

void TelescopeBridge::handleDescription(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    std::string description = std::string("INDI Telescope: ") + m_Device.getDeviceName();
    sendResponseValue(res, description);
}

void TelescopeBridge::handleDriverInfo(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    std::string driverInfo = "INDI Alpaca Bridge";
    sendResponseValue(res, driverInfo);
}

void TelescopeBridge::handleDriverVersion(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    std::string version = "2.0";
    sendResponseValue(res, version);
}

void TelescopeBridge::handleInterfaceVersion(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    sendResponseValue(res, 3);
}

// Telescope-specific properties
void TelescopeBridge::handleAlignmentMode(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);

    // Check if the telescope has a mount type property
    auto mountTypeSw = m_Device.getSwitch("TELESCOPE_MOUNT_TYPE");
    if (mountTypeSw)
    {
        INDI::PropertySwitch switchProperty(mountTypeSw);

        // ASCOM AlignmentMode values:
        // 0 = Alt/Az alignment
        // 1 = Polar (equatorial) alignment
        // 2 = German polar alignment

        // Check which mount type is active
        sendResponseValue(res, switchProperty.findOnSwitchIndex());
    }
    else
    {
        // If no mount type property is available, check if the telescope has horizontal coordinates
        bool hasHorizontalCoord = m_Device.getProperty("HORIZONTAL_COORD").isValid();
        bool hasEquatorialCoord = m_Device.getProperty("EQUATORIAL_EOD_COORD").isValid();

        if (hasHorizontalCoord && !hasEquatorialCoord)
        {
            sendResponseValue(res, 0); // Alt/Az alignment
        }
        else
        {
            // Default to German equatorial alignment
            sendResponseValue(res, 2);
        }
    }
}

void TelescopeBridge::handleAltitude(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    std::lock_guard<std::mutex> lock(m_Mutex);
    sendResponseValue(res, m_CurrentALT);
}

void TelescopeBridge::handleAzimuth(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    std::lock_guard<std::mutex> lock(m_Mutex);
    sendResponseValue(res, m_CurrentAZ);
}

void TelescopeBridge::handleCanPark(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    sendResponseValue(res, m_Device.getProperty("TELESCOPE_PARK").isValid());
}

void TelescopeBridge::handleCanPulseGuide(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    bool canPulseGuide = m_Device.getProperty("TELESCOPE_TIMED_GUIDE_NS").isValid() &&
                         m_Device.getProperty("TELESCOPE_TIMED_GUIDE_WE").isValid();
    sendResponseValue(res, canPulseGuide);
}

void TelescopeBridge::handleCanSetTracking(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    sendResponseValue(res, m_Device.getProperty("TELESCOPE_TRACK_STATE").isValid());
}

void TelescopeBridge::handleCanSetRightAscensionRate(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    sendResponseValue(res, m_Device.getProperty("TELESCOPE_TRACK_RATE").isValid());
}

void TelescopeBridge::handleCanSetDeclinationRate(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    sendResponseValue(res, m_Device.getProperty("TELESCOPE_TRACK_RATE").isValid());
}

void TelescopeBridge::handleCanSetGuideRates(const httplib::Request &req, httplib::Response &res) {
    INDI_UNUSED(req);
    sendResponseValue(res, m_Device.getProperty("GUIDE_RATE").isValid()); 
}

void TelescopeBridge::handleCanSetPark(const httplib::Request &req, httplib::Response &res) {
    INDI_UNUSED(req);
    sendResponseValue(res, m_Device.getProperty("TELESCOPE_PARK_OPTION").isValid()); 
}

void TelescopeBridge::handleCanSlew(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    sendResponseValue(res, m_Device.getProperty("EQUATORIAL_EOD_COORD").isValid());
}

void TelescopeBridge::handleCanFindHome(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    bool canFindHome=false;
    INDI::Property property = m_Device.getProperty("TELESCOPE_HOME"); 
    // For Alpaca findhome is just as INDI GO HOME and
    // because INDI SET HOME (user defined one) is not defined, neither INDI FIND HOME (mount defined one) 
    // So we don't check FIND switch property
    // if (property.isValid()) {
    //     INDI::PropertySwitch switchProperty(property);
    //     for (auto &num : switchProperty)
    //     {
    //         if (num.isNameMatch("FIND"))
    //             canFindHome = true;
    //     }
    // }
    canFindHome=property.isValid();
    sendResponseValue(res, canFindHome);
}

void TelescopeBridge::handleCanUnpark(const httplib::Request &req, httplib::Response &res) {
    INDI_UNUSED(req);
    sendResponseValue(res, m_Device.getProperty("TELESCOPE_PARK").isValid());        
}

void TelescopeBridge::handleCanMoveAxis(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    sendResponseValue(res, m_Device.getProperty("EQUATORIAL_EOD_COORD").isValid());
}

void TelescopeBridge::handleCanSlewAsync(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    sendResponseValue(res, m_Device.getProperty("EQUATORIAL_EOD_COORD").isValid());
}

void TelescopeBridge::handleCanSlewAltAzAsync(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    sendResponseValue(res, m_Device.getProperty("HORIZONTAL_COORD").isValid());
}

void TelescopeBridge::handleCanSync(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    sendResponseValue(res, m_Device.getProperty("ON_COORD_SET").isValid());
}

void TelescopeBridge::handleDeclination(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    std::lock_guard<std::mutex> lock(m_Mutex);
    sendResponseValue(res, m_CurrentDEC);    
}

void TelescopeBridge::handleDeclinationRate(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    std::lock_guard<std::mutex> lock(m_Mutex);
    sendResponseValue(res, m_DeclinationRateAlpaca); 
}

void TelescopeBridge::handleRightAscension(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    std::lock_guard<std::mutex> lock(m_Mutex);
    sendResponseValue(res, m_CurrentRA);       
}

void TelescopeBridge::handleRightAscensionRate(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    std::lock_guard<std::mutex> lock(m_Mutex);
    sendResponseValue(res, m_RightAscensionRateAlpaca); 
}

void TelescopeBridge::handleSideOfPier(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    std::lock_guard<std::mutex> lock(m_Mutex);
    sendResponseValue(res, m_PierSide);
}

void TelescopeBridge::handleSlewing(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    std::lock_guard<std::mutex> lock(m_Mutex);
    sendResponseValue(res, m_IsMotionSlewing || m_IsCoordsSlewing);
}

void TelescopeBridge::handleTracking(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    std::lock_guard<std::mutex> lock(m_Mutex);
    sendResponseValue(res, m_IsTracking);
}

void TelescopeBridge::handleTrackingRates(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);

    json trRatesArray = json::array();
    // fallback
    trRatesArray.push_back(0);  // Alpaca driveSidereal

    auto trackModeSw = m_Device.getSwitch("TELESCOPE_TRACK_MODE");
    if (trackModeSw)
    {
        for (auto &wvs:trackModeSw) {
            if (wvs.isNameMatch("TRACK_SIDEREAL")) {
                // already set
            } 
            else if (wvs.isNameMatch("TRACK_LUNAR")) {
                trRatesArray.push_back(1);  // Alpaca driveLunar
            } 
            else if (wvs.isNameMatch("TRACK_SOLAR")) {
                trRatesArray.push_back(2);  // Alpaca driveSolar
            } 
            else if (wvs.isNameMatch("TRACK_KING")) {
                trRatesArray.push_back(3);  // Alpaca driveKing
            } 
            // Note that Indi TRACK_CUSTOM is handled in Alpaca 
            // using driveSidereal rate and DeclinationRate and RightAscensionRate values
        }
    }           
    sendResponseValue(res, trRatesArray);    
}

void TelescopeBridge::handleTrackingRate(const httplib::Request &req, httplib::Response &res) 
{
    if (req.method=="PUT") {
        std::map<std::string, std::string> formData;
        DeviceManager::parseFormUrlEncodedBody(req.body, formData);

        // Check if required parameters exist
        if (formData.find("TrackingRate") == formData.end())
        {
            sendResponseStatus(res, false, "Missing TrackingRate parameter");
            return;
        }
        else {
            bool success = false;
            bool rate_supported=false;
            int tracking_rate = std::stoi(formData["TrackingRate"]);
            auto trackModeSw = m_Device.getSwitch("TELESCOPE_TRACK_MODE");
            if (trackModeSw)
            {
                int i=-1;
                int j=-1;
                for (auto &wvs: trackModeSw) {
                    i++;
                    if (tracking_rate == 0 && wvs.isNameMatch("TRACK_SIDEREAL")) {
                        j=i; 
                        rate_supported=true;
                        break;
                    }
                    if (tracking_rate == 1 && wvs.isNameMatch("TRACK_LUNAR")) {
                        j=i; 
                        rate_supported=true;
                        break;
                    }
                    if (tracking_rate == 2 && wvs.isNameMatch("TRACK_SOLAR")) {
                        j=i; 
                        rate_supported=true;
                        break;
                    }
                    if (tracking_rate == 3 && wvs.isNameMatch("TRACK_KING")) {
                        j=i; 
                        rate_supported=true;
                        break;
                    }                                        
                }
                if (j>=0) {
                    INDI::PropertySwitch switchProperty(trackModeSw);
                    switchProperty.reset();
                    switchProperty[j].setState(ISS_ON);
                    // Request to send the switch to the device
                    requestNewSwitch(switchProperty);
                    success = true;
                }
            } 
            std::string errorMessage=success ? "" : rate_supported ? "Failed to set TrackingRate" : "Tracking rate not supported: " + std::to_string(tracking_rate);
            sendResponseStatus(res, success, errorMessage, rate_supported ? 0 : 1025); // 1025 is InvalidValueException
            return;
        }
    }
    else {
        int tracking_rate = 0; // fallback, Alpaca driveSidereal
        auto trackModeSw = m_Device.getSwitch("TELESCOPE_TRACK_MODE");
        if (trackModeSw) {
            for (auto &wvs: trackModeSw) {
                if (wvs.isNameMatch("TRACK_SIDEREAL")) {
                    if (wvs.getState()==ISS_ON) {
                        tracking_rate=0; // Alpaca driveSidereal
                        break;
                    }
                }
                else if (wvs.isNameMatch("TRACK_LUNAR")) {
                    if (wvs.getState()==ISS_ON) {
                        tracking_rate=1; // Alpaca driveLunar
                        break;
                    }
                }
                else if (wvs.isNameMatch("TRACK_SOLAR")) {
                    if (wvs.getState()==ISS_ON) {
                        tracking_rate=2; // Alpaca driveSolar
                        break;
                    }
                }
                else if (wvs.isNameMatch("TRACK_KING")) {
                    if (wvs.getState()==ISS_ON) {
                        tracking_rate=3; // Alpaca driveKing
                        break;
                    }
                }
                else if (wvs.isNameMatch("TRACK_CUSTOM")) {
                    if (wvs.getState()==ISS_ON) {
                        tracking_rate=0; // Alpaca driveSidereal (note that Alpaca uses DeclinationRate and RightAscensionRate)
                        break;
                    }
                }
            }
        }
        sendResponseValue(res, tracking_rate);
    }
}


void TelescopeBridge::handleAtPark(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    std::lock_guard<std::mutex> lock(m_Mutex);
    sendResponseValue(res, m_IsParked);
}

void TelescopeBridge::handleAtHome(const httplib::Request &req, httplib::Response &res) {
    INDI_UNUSED(req);
    std::lock_guard<std::mutex> lock(m_Mutex);
    sendResponseValue(res, m_IsAtHome);    
}

// Telescope-specific actions
void TelescopeBridge::handleAbortSlew(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    bool success = false;
    auto abortSw = m_Device.getSwitch("TELESCOPE_ABORT_MOTION");
    if (abortSw)
    {
        INDI::PropertySwitch switchProperty(abortSw);
        switchProperty.reset();
        switchProperty[0].setState(ISS_ON);
        // Request to send the switch to the device
        requestNewSwitch(switchProperty);
        success = true;
    }

    sendResponseStatus(res, success, success ? "" : "Failed to abort slew");
}

void TelescopeBridge::handlePark(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    std::lock_guard<std::mutex> lock(m_Mutex);
    bool success = m_IsParked;
    if (!success) {
        auto parkSw = m_Device.getSwitch("TELESCOPE_PARK");
        if (parkSw)
        {
            INDI::PropertySwitch switchProperty(parkSw);
            switchProperty.reset();
            switchProperty[0].setState(ISS_ON);
            // Request to send the switch to the device
            requestNewSwitch(switchProperty);
            success = true;
        }
    }
    sendResponseStatus(res, success, success ? "" : "Failed to park telescope");
}

void TelescopeBridge::handleFindHome(const httplib::Request &req, httplib::Response &res) 
{
    INDI_UNUSED(req);
    bool success = false;
    auto homeSw = m_Device.getSwitch("TELESCOPE_HOME");
    if (homeSw)
    {
        INDI::PropertySwitch switchProperty(homeSw);
        // Search for GO WidgetViewSwitch (some drives lack FIND, but have GO)
        int i=-1;
        int j=-1;
        for(auto &wvs: homeSw) {
            j++;
            if (wvs.isNameMatch("GO")) {
                i=j;
                break;
            }
        }
        // Apply GO HOME action
        if (i>=0) {
            switchProperty.reset();
            switchProperty[i].setState(ISS_ON);
            requestNewSwitch(switchProperty);
            success = true;
        }
    }

    sendResponseStatus(res, success, success ? "" : "Failed to send to home telescope");    
}

void TelescopeBridge::handleUnpark(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    bool success = false;
    auto parkSw = m_Device.getSwitch("TELESCOPE_PARK");
    if (parkSw)
    {
        INDI::PropertySwitch switchProperty(parkSw);
        switchProperty.reset();
        switchProperty[1].setState(ISS_ON);
        // Request to send the switch to the device
        requestNewSwitch(switchProperty);
        success = true;
    }

    sendResponseStatus(res, success, success ? "" : "Failed to unpark telescope");
}

void TelescopeBridge::handleSetPark(const httplib::Request &req, httplib::Response &res) {
    INDI_UNUSED(req);
    bool success = false;
    auto parkOptionSw = m_Device.getSwitch("TELESCOPE_PARK_OPTION");
    if (parkOptionSw)
    {
        INDI::PropertySwitch switchProperty(parkOptionSw);
        // Search for PARK_CURRENT WidgetViewSwitch
        int i=-1;
        int j=-1;
        for(auto &wvs: parkOptionSw) {
            j++;
            if (wvs.isNameMatch("PARK_CURRENT")) {
                i=j;
                break;
            }
        }
        // Apply PARK_CURRENT action
        if (i>=0) {
            switchProperty.reset();
            switchProperty[i].setState(ISS_ON);
            requestNewSwitch(switchProperty);
            success = true;
        }
    }

    sendResponseStatus(res, success, success ? "" : "Failed to set park position for telescope");      
}

void TelescopeBridge::handleUTCDate(const httplib::Request &req, httplib::Response &res) {
    INDI_UNUSED(req);
    std::string utc_alpaca="1970-01-01T00:00:00Z"; //fallback
    auto utcTxt = m_Device.getText("TIME_UTC");
    if (utcTxt) {
        for(auto &wvt: utcTxt) {
            if (wvt.isNameMatch("UTC")) {
                std::string utc_indi = wvt.getText(); // like 2026-05-22T22:09:28+00:00             
                size_t pos = utc_indi.rfind("+00:00"); // Find the offset at the end   
                if (pos != std::string::npos) {
                    // Replace 6 characters starting at 'pos' with "Z"
                    utc_alpaca=utc_indi.replace(pos, 6, "Z");
                }               
                break;
            }
        }
    }
    // return value
    sendResponseValue(res,utc_alpaca);
}

void TelescopeBridge::handleSiderealTime(const httplib::Request &req, httplib::Response &res) {
    INDI_UNUSED(req);
    // Sidereal time is taken from Number if available, otherwise it is computed
    bool computeFlag = true;
    double lst_alpaca;
    auto lstNumber = m_Device.getNumber("TIME_LST");
    if (lstNumber.isValid()) {
        for(auto &wvn: lstNumber) {
            if (wvn.isNameMatch("LST")) {
                lst_alpaca = wvn.getValue(); 
                computeFlag=false;
                break;
            }
        }                
    } 
    if (computeFlag) {
        auto geoCoord = m_Device.getNumber("GEOGRAPHIC_COORD");
        if (!geoCoord.isValid())
        {
            sendResponseValue(res, 0.0, false, "GEOGRAPHIC_COORD property not found");
            return;
        }        
        double longitudeDeg = 0.0;
        INDI::PropertyNumber numberProperty(geoCoord);
        for (auto &num : numberProperty)
        {
            if (num.isNameMatch("LONG"))
            {
                longitudeDeg = num.getValue();
                break;
            }
        }     
        lst_alpaca = computeLST(longitudeDeg);        
    }
    sendResponseValue(res,lst_alpaca); 
}

void TelescopeBridge::handleSlewToCoordinates(const httplib::Request &req, httplib::Response &res)
{
    bool success = false;
    try
    {
        // Parse form-urlencoded data from request body
        std::map<std::string, std::string> formData;
        DeviceManager::parseFormUrlEncodedBody(req.body, formData);

        // Check if required parameters exist
        if (formData.find("RightAscension") == formData.end() || formData.find("Declination") == formData.end())
        {
            sendResponseStatus(res, false, "Missing RightAscension or Declination parameter");
            return;
        }

        // Convert values to double
        double ra = std::stod(formData["RightAscension"]);
        double dec = std::stod(formData["Declination"]);

        auto coordSet = m_Device.getSwitch("ON_COORD_SET");
        auto eqCoord = m_Device.getNumber("EQUATORIAL_EOD_COORD");
        if (coordSet && eqCoord)
        {
            // Set to TRACK mode
            INDI::PropertySwitch switchProperty(coordSet);
            switchProperty.reset();
            switchProperty[0].setState(ISS_ON);
            // Request to send the switch to the device
            requestNewSwitch(switchProperty);

            // Set coordinates
            INDI::PropertyNumber numberProperty(eqCoord);
            for (auto &num : numberProperty)
            {
                if (num.isNameMatch("RA"))
                    num.setValue(ra);
                else if (num.isNameMatch("DEC"))
                    num.setValue(dec);
            }
            // Request to send the coordinates to the device
            requestNewNumber(numberProperty);
            success = true;

            // Update target coordinates
            m_TargetRA = ra;
            m_TargetDEC = dec;
        }
    }
    catch (const std::exception &e)
    {
        DEBUGFDEVICE(m_Device.getDeviceName(), INDI::Logger::DBG_ERROR, "Failed to parse SlewToCoordinates request: %s", e.what());
    }

    sendResponseStatus(res, success, success ? "" : "Failed to slew to coordinates");
}

void TelescopeBridge::handleSlewToCoordinatesAsync(const httplib::Request &req, httplib::Response &res)
{
    // Same implementation as SlewToCoordinates for now
    handleSlewToCoordinates(req, res);
}

void TelescopeBridge::handleSyncToCoordinates(const httplib::Request &req, httplib::Response &res)
{
    bool success = false;
    try
    {
        // Parse form-urlencoded data from request body
        std::map<std::string, std::string> formData;
        DeviceManager::parseFormUrlEncodedBody(req.body, formData);

        // Check if required parameters exist
        if (formData.find("RightAscension") == formData.end() || formData.find("Declination") == formData.end())
        {
            sendResponseStatus(res, false, "Missing RightAscension or Declination parameter");
            return;
        }

        // Convert values to double
        double ra = std::stod(formData["RightAscension"]);
        double dec = std::stod(formData["Declination"]);

        auto coordSet = m_Device.getSwitch("ON_COORD_SET");
        auto eqCoord = m_Device.getNumber("EQUATORIAL_EOD_COORD");
        if (coordSet && eqCoord)
        {
            // Set to SYNC mode
            INDI::PropertySwitch switchProperty(coordSet);
            switchProperty.reset();
            switchProperty[1].setState(ISS_ON);
            // Request to send the switch to the device
            requestNewSwitch(switchProperty);

            // Set coordinates
            INDI::PropertyNumber numberProperty(eqCoord);
            for (auto &num : numberProperty)
            {
                if (num.isNameMatch("RA"))
                    num.setValue(ra);
                else if (num.isNameMatch("DEC"))
                    num.setValue(dec);
            }
            // Request to send the coordinates to the device
            requestNewNumber(numberProperty);
            success = true;
        }
    }
    catch (const std::exception &e)
    {
        DEBUGFDEVICE(m_Device.getDeviceName(), INDI::Logger::DBG_ERROR, "Failed to parse SyncToCoordinates request: %s", e.what());
    }

    sendResponseStatus(res, success, success ? "" : "Failed to sync to coordinates");
}

void TelescopeBridge::handlePulseGuide(const httplib::Request &req, httplib::Response &res)
{
    bool success = false;
    try
    {
        // Parse form-urlencoded data from request body
        std::map<std::string, std::string> formData;
        DeviceManager::parseFormUrlEncodedBody(req.body, formData);

        // Check if required parameters exist
        if (formData.find("Direction") == formData.end() || formData.find("Duration") == formData.end())
        {
            sendResponseStatus(res, false, "Missing Direction or Duration parameter");
            return;
        }

        // Convert values to integers
        int direction = std::stoi(formData["Direction"]);
        int duration = std::stoi(formData["Duration"]);

        // Direction: 0=North, 1=South, 2=East, 3=West
        if (direction == 0 || direction == 1)
        {
            auto guideNS = m_Device.getNumber("TELESCOPE_TIMED_GUIDE_NS");
            if (guideNS)
            {
                INDI::PropertyNumber numberProperty(guideNS);
                numberProperty[direction].setValue(duration);
                // Request to send the guide command to the device
                requestNewNumber(numberProperty);
                success = true;
            }
        }
        else if (direction == 2 || direction == 3)
        {
            auto guideWE = m_Device.getNumber("TELESCOPE_TIMED_GUIDE_WE");
            if (guideWE)
            {
                INDI::PropertyNumber numberProperty(guideWE);
                numberProperty[direction - 2].setValue(duration);
                // Request to send the guide command to the device
                requestNewNumber(numberProperty);
                success = true;
            }
        }
    }
    catch (const std::exception &e)
    {
        DEBUGFDEVICE(m_Device.getDeviceName(), INDI::Logger::DBG_ERROR, "Failed to parse PulseGuide request: %s", e.what());
    }

    sendResponseStatus(res, success, success ? "" : "Failed to pulse guide");
}

void TelescopeBridge::handleMoveAxis(const httplib::Request &req, httplib::Response &res)
{
    bool success = false;
    try
    {
        // Parse form-urlencoded data from request body
        std::map<std::string, std::string> formData;
        DeviceManager::parseFormUrlEncodedBody(req.body, formData);

        // Check if required parameters exist
        if (formData.find("Axis") == formData.end() || formData.find("Rate") == formData.end())
        {
            sendResponseStatus(res, false, "Missing Axis or Rate parameter");
            return;
        }

        // Convert values
        int axis = std::stoi(formData["Axis"]);
        double rate = std::stod(formData["Rate"]);

        // Get the slew rate property
        auto slewRateSP = m_Device.getSwitch("TELESCOPE_SLEW_RATE");

        // Try to set an appropriate slew rate based on the magnitude of the rate
        // Only if we can determine a good conversion
        if (slewRateSP)
        {
            INDI::PropertySwitch slewRateProperty(slewRateSP);
            int numRates = slewRateProperty.count();

            // Only attempt to set slew rate if we have a non-zero rate
            if (numRates > 0 && std::fabs(rate) > 0)
            {
                // The sidereal rate is approximately 0.004178 degrees/second
                const double SIDEREAL_RATE_DEG_SEC = 0.004178;

                // Calculate how many times the sidereal rate this is
                double rateMultiple = std::fabs(rate) / SIDEREAL_RATE_DEG_SEC;

                // Try to find the best matching slew rate
                int bestRateIndex = -1;
                double bestRateDiff = std::numeric_limits<double>::max();

                // First, try to extract rate multiples from the labels (e.g., "200x", "400x", etc.)
                std::vector<double> rateMultiples;
                rateMultiples.resize(numRates, 0);
                bool hasExplicitMultiples = false;

                for (int i = 0; i < numRates; i++)
                {
                    std::string label = slewRateProperty[i].getLabel();
                    // Look for patterns like "200x" or "400x" in the label
                    std::size_t xPos = label.find('x');
                    if (xPos != std::string::npos && xPos > 0)
                    {
                        // Try to extract the number before the 'x'
                        std::string numStr = label.substr(0, xPos);
                        // Remove any non-numeric characters
                        numStr.erase(std::remove_if(numStr.begin(), numStr.end(),
                                                    [](char c)
                        {
                            return !std::isdigit(c);
                        }),
                        numStr.end());

                        if (!numStr.empty())
                        {
                            try
                            {
                                double multiple = std::stod(numStr);
                                rateMultiples[i] = multiple;
                                hasExplicitMultiples = true;

                                // Calculate how close this rate is to our target
                                double diff = std::fabs(multiple - rateMultiple);
                                if (diff < bestRateDiff)
                                {
                                    bestRateDiff = diff;
                                    bestRateIndex = i;
                                }

                                DEBUGFDEVICE(m_Device.getDeviceName(), INDI::Logger::DBG_DEBUG,
                                             "Found slew rate multiple in label: %s = %.0fx",
                                             label.c_str(), multiple);
                            }
                            catch (const std::exception &e)
                            {
                                // Failed to convert to number, ignore this label
                            }
                        }
                    }
                }

                // If we couldn't extract multiples from labels, use our default mapping
                if (!hasExplicitMultiples)
                {
                    // Common slew rate multiples for different mounts
                    // Guide: ~1x, Centering: ~8x, Find: ~16x, Max: ~64x or higher
                    // These are approximate and vary by mount

                    // Select appropriate slew rate based on the multiple
                    if (rateMultiple <= 2)
                        bestRateIndex = 0; // Guide rate (slowest)
                    else if (rateMultiple <= 10)
                        bestRateIndex = std::min(1, numRates - 1); // Centering rate
                    else if (rateMultiple <= 30)
                        bestRateIndex = std::min(2, numRates - 1); // Find rate
                    else
                        bestRateIndex = numRates - 1; // Max rate (fastest)

                    DEBUGFDEVICE(m_Device.getDeviceName(), INDI::Logger::DBG_DEBUG,
                                 "Using default slew rate mapping for %.1fx sidereal", rateMultiple);
                }

                // Only set the slew rate if we determined a valid index
                if (bestRateIndex >= 0)
                {
                    slewRateProperty.reset();
                    slewRateProperty[bestRateIndex].setState(ISS_ON);
                    requestNewSwitch(slewRateProperty);

                    DEBUGFDEVICE(m_Device.getDeviceName(), INDI::Logger::DBG_DEBUG,
                                 "Setting slew rate to index %d (%s) for rate %.4f deg/sec (%.1fx sidereal)",
                                 bestRateIndex, slewRateProperty[bestRateIndex].getLabel(),
                                 std::fabs(rate), rateMultiple);
                }
            }
        }

        // Axis: 0=Primary (RA/AZ), 1=Secondary (DEC/ALT)
        if (axis == 0)
        {
            auto motionWE = m_Device.getSwitch("TELESCOPE_MOTION_WE");
            if (motionWE)
            {
                INDI::PropertySwitch switchProperty(motionWE);
                switchProperty.reset();
                if (rate > 0)
                    switchProperty[0].setState(ISS_ON); // West
                else if (rate < 0)
                    switchProperty[1].setState(ISS_ON); // East
                else
                {
                    // If rate is 0, stop motion
                    switchProperty[0].setState(ISS_OFF);
                    switchProperty[1].setState(ISS_OFF);
                }
                // Request to send the switch to the device
                requestNewSwitch(switchProperty);
                success = true;
            }
        }
        else if (axis == 1)
        {
            auto motionNS = m_Device.getSwitch("TELESCOPE_MOTION_NS");
            if (motionNS)
            {
                INDI::PropertySwitch switchProperty(motionNS);
                switchProperty.reset();
                if (rate > 0)
                    switchProperty[0].setState(ISS_ON); // North
                else if (rate < 0)
                    switchProperty[1].setState(ISS_ON); // South
                else
                {
                    // If rate is 0, stop motion
                    switchProperty[0].setState(ISS_OFF);
                    switchProperty[1].setState(ISS_OFF);
                }
                // Request to send the switch to the device
                requestNewSwitch(switchProperty);
                success = true;
            }
        }
    }
    catch (const std::exception &e)
    {
        DEBUGFDEVICE(m_Device.getDeviceName(), INDI::Logger::DBG_ERROR, "Failed to parse MoveAxis request: %s", e.what());
    }

    sendResponseStatus(res, success, success ? "" : "Failed to move axis");
}

void TelescopeBridge::handleAxisRates(const httplib::Request &req, httplib::Response &res)
{
    try
    {
        // Check if the Axis parameter exists in the query
        auto axisIt = req.params.find("Axis");
        if (axisIt == req.params.end())
        {
            sendResponseValue(res, json::array(), false, "Missing Axis parameter");
            return;
        }

        // Convert Axis value to int
        int axis = std::stoi(axisIt->second);

        // Validate axis value (0 = Primary/RA/AZ, 1 = Secondary/DEC/ALT)
        if (axis < 0 || axis > 1)
        {
            sendResponseValue(res, json::array(), false, "Invalid Axis value. Must be 0 (Primary/RA/AZ) or 1 (Secondary/DEC/ALT)");
            return;
        }

        // Create a JSON array of rate objects
        json ratesArray = json::array();

        // 1x to 800x sidereal
        // hardcoded for now
        ratesArray.push_back(
        {
            {"Minimum", 0.00418}, // degrees/sec
            {"Maximum", 3.344}
        });

        // Send the response
        sendResponseValue(res, ratesArray);
    }
    catch (const std::exception &e)
    {
        DEBUGFDEVICE(m_Device.getDeviceName(), INDI::Logger::DBG_ERROR, "Failed to process AxisRates request: %s", e.what());
        sendResponseValue(res, json::array(), false, std::string("Error processing request: ") + e.what());
    }
}

void TelescopeBridge::handleSetTracking(const httplib::Request &req, httplib::Response &res)
{
    bool success = false;
    try
    {
        // Parse form-urlencoded data from request body
        std::map<std::string, std::string> formData;
        DeviceManager::parseFormUrlEncodedBody(req.body, formData);

        // Check if required parameters exist
        if (formData.find("Tracking") == formData.end())
        {
            sendResponseStatus(res, false, "Missing Tracking parameter");
            return;
        }

        // Convert Tracking value to bool
        bool tracking = (formData["Tracking"] == "True" || formData["Tracking"] == "true" ||
                         formData["Tracking"] == "1");

        auto trackState = m_Device.getSwitch("TELESCOPE_TRACK_STATE");
        if (trackState)
        {
            INDI::PropertySwitch switchProperty(trackState);
            switchProperty.reset();
            switchProperty[tracking ? 0 : 1].setState(ISS_ON);
            // Request to send the switch to the device
            requestNewSwitch(switchProperty);
            success = true;
        }
    }
    catch (const std::exception &e)
    {
        DEBUGFDEVICE(m_Device.getDeviceName(), INDI::Logger::DBG_ERROR, "Failed to parse SetTracking request: %s", e.what());
    }

    sendResponseStatus(res, success, success ? "" : "Failed to set tracking state");
}

void TelescopeBridge::handleSetUTCDate(const httplib::Request &req, httplib::Response &res) {
    bool success = false;
    try
    {
        // Parse form-urlencoded data from request body
        std::map<std::string, std::string> formData;
        DeviceManager::parseFormUrlEncodedBody(req.body, formData);

        // Check if required parameters exist
        if (formData.find("UTCDate") == formData.end())
        {
            sendResponseStatus(res, false, "Missing UTCDate parameter");
            return;
        }

        std::string utc_alpaca = formData["UTCDate"];

        auto utcTxt = m_Device.getText("TIME_UTC");
        if (utcTxt)
        {
            std::string utc_indi="";
            if (!utc_alpaca.empty() && utc_alpaca.back() == 'Z') {
                // Replace the last 1 character with "+00:00"
                utc_indi = utc_alpaca.replace(utc_alpaca.length() - 1, 1, "+00:00");
            }            
            if (!utc_indi.empty()) {

                INDI::PropertyText textProperty(utcTxt);
                textProperty[0].setText(utc_indi);
                // Request to send the text to the device
                requestNewText(textProperty);                  
                success = true;
            }
        }
    }
    catch (const std::exception &e)
    {
        DEBUGFDEVICE(m_Device.getDeviceName(), INDI::Logger::DBG_ERROR, "Failed to parse UTCDate request: %s", e.what());
    }

    sendResponseStatus(res, success, success ? "" : "Failed to set tracking state");
}

void TelescopeBridge::handleSetRightAscensionRate(const httplib::Request &req, httplib::Response &res)
{
    bool success = false;
    try
    {
        // Parse form-urlencoded data from request body
        std::map<std::string, std::string> formData;
        DeviceManager::parseFormUrlEncodedBody(req.body, formData);

        // Check if required parameters exist
        if (formData.find("RightAscensionRate") == formData.end())
        {
            sendResponseStatus(res, false, "Missing RightAscensionRate parameter");
            return;
        }
        
        // Convert RightAscensionRate value to double
        double raRate = std::stod(formData["RightAscensionRate"]);

        // Set Tracking mode to CUSTOM   
        auto trackModeSw = m_Device.getSwitch("TELESCOPE_TRACK_MODE");
        if (trackModeSw) {
            int j=-1;
            for (auto &wvs :trackModeSw) {
                j++;
                if ( (raRate==0.0 && wvs.isNameMatch("TRACK_SIDEREAL")) || (raRate!=0.0 && wvs.isNameMatch("TRACK_CUSTOM")) ) {
        
                    // // disable tracking, if active
                    // bool trackingWasActive = false;            
                    // auto trackState = m_Device.getSwitch("TELESCOPE_TRACK_STATE");
                    // if (trackState)
                    // {
                    //     if (trackState[0].getState()==ISS_ON) trackingWasActive=true;
                    //     if (trackingWasActive) {
                    //         INDI::PropertySwitch switchProperty(trackState);
                    //         switchProperty.reset();
                    //         switchProperty[1].setState(ISS_ON);
                    //         // Request to send the switch to the device
                    //         requestNewSwitch (switchProperty);
                    //     }
                    // }

                    INDI::PropertySwitch switchProperty(trackModeSw);
                    switchProperty.reset();
                    switchProperty[j].setState(ISS_ON);
                    requestNewSwitch(switchProperty);

                    double raRate1 = 15.041067 - 15.0 * raRate; 
                    auto trackRate = m_Device.getNumber("TELESCOPE_TRACK_RATE");
                    if (trackRate)
                    {
                        INDI::PropertyNumber numberProperty(trackRate);
                        for (auto &num : numberProperty)
                        {
                            if (num.isNameMatch("TRACK_RATE_RA"))
                                num.setValue(raRate1);
                        }
                        // Request to send the rate to the device
                        requestNewNumber(numberProperty);
                        
                        // Update the member variable
                        m_RightAscensionRate = raRate1;
                        m_RightAscensionRateAlpaca = raRate;

                        // // enable tracking, if it was disabled before
                        // if (trackState && trackingWasActive) {
                        //     INDI::PropertySwitch switchProperty(trackState);
                        //     switchProperty.reset();
                        //     switchProperty[0].setState(ISS_ON);
                        //     // Request to send the switch to the device
                        //     requestNewSwitch(switchProperty);
                        // }                        
                        success = true;
                        break;
                    }
                  
                }
            }
        }

    }
    catch (const std::exception &e)
    {
        DEBUGFDEVICE(m_Device.getDeviceName(), INDI::Logger::DBG_ERROR, "Failed to parse SetRightAscensionRate request: %s",
                     e.what());
    }

    sendResponseStatus(res, success, success ? "" : "Failed to set right ascension rate");
}

void TelescopeBridge::handleSetDeclinationRate(const httplib::Request &req, httplib::Response &res)
{
    bool success = false;
    try
    {
        // Parse form-urlencoded data from request body
        std::map<std::string, std::string> formData;
        DeviceManager::parseFormUrlEncodedBody(req.body, formData);

        // Check if required parameters exist
        if (formData.find("DeclinationRate") == formData.end())
        {
            sendResponseStatus(res, false, "Missing DeclinationRate parameter");
            return;
        }

        // Convert DeclinationRate value to double
        double decRate = std::stod(formData["DeclinationRate"]);

        // Set Tracking mode to CUSTOM
        auto trackModeSw = m_Device.getSwitch("TELESCOPE_TRACK_MODE");
        if (trackModeSw) {
            int j=-1;
            for (auto &wvs :trackModeSw) {
                j++;
                if (wvs.isNameMatch("TRACK_CUSTOM")) {
                  
                    INDI::PropertySwitch switchProperty(trackModeSw);
                    switchProperty.reset();
                    switchProperty[j].setState(ISS_ON);
                    requestNewSwitch(switchProperty);

                    // detect PIER side and also change sign when PIER side is WEST
                    double sign=1.0; 
                    auto pierSideSw = m_Device.getSwitch("TELESCOPE_PIER_SIDE");
                    if (pierSideSw) {
                        for (auto wvsp:pierSideSw) {
                            if (wvsp.isNameMatch("PIER_WEST"))
                                if (wvsp.getState()==ISS_ON) {
                                    sign=-1.0;
                                    break;
                                }
                        }
                    }                    

                    double decRate1 = sign * decRate;
                    auto trackRate = m_Device.getNumber("TELESCOPE_TRACK_RATE");
                    if (trackRate)
                    {
                        INDI::PropertyNumber numberProperty(trackRate);
                        for (auto &num : numberProperty)
                        {
                            if (num.isNameMatch("TRACK_RATE_DE"))
                                num.setValue(decRate1);
                        }
                        // Request to send the rate to the device
                        requestNewNumber(numberProperty);

                        // Update the member variable (keep Indi value here)
                        m_DeclinationRate = decRate1;
                        m_DeclinationRateAlpaca = decRate;
                        success = true;
                        break;
                    }
                  
                }
            }
        }

    }
    catch (const std::exception &e)
    {
        DEBUGFDEVICE(m_Device.getDeviceName(), INDI::Logger::DBG_ERROR, "Failed to parse SetDeclinationRate request: %s",
                     e.what());
    }

    sendResponseStatus(res, success, success ? "" : "Failed to set declination rate");
}

// Equatorial System
void TelescopeBridge::handleEquatorialSystem(const httplib::Request &req, httplib::Response &res)
{
    INDI_UNUSED(req);
    std::lock_guard<std::mutex> lock(m_Mutex);
    sendResponseValue(res, 1);
}

// Site Information
void TelescopeBridge::handleSiteLatitude(const httplib::Request &req, httplib::Response &res)
{
    auto geoCoord = m_Device.getNumber("GEOGRAPHIC_COORD");
    if (!geoCoord.isValid())
    {
        sendResponseValue(res, 0.0, false, "GEOGRAPHIC_COORD property not found");
        return;
    }

    // Handle PUT request to set site latitude
    if (req.method == "PUT")
    {
        try
        {
            // Parse form-urlencoded data from request body
            std::map<std::string, std::string> formData;
            DeviceManager::parseFormUrlEncodedBody(req.body, formData);

            // Check if SiteLatitude parameter exists
            if (formData.find("SiteLatitude") == formData.end())
            {
                sendResponseValue(res, 0.0, false, "Missing SiteLatitude parameter");
                return;
            }

            // Convert SiteLatitude value to double
            double latitude = std::stod(formData["SiteLatitude"]);

            // Update the GEOGRAPHIC_COORD property
            INDI::PropertyNumber numberProperty(geoCoord);
            for (auto &num : numberProperty)
            {
                if (num.isNameMatch("LAT"))
                    num.setValue(latitude);
            }

            // Request to send the updated property to the device
            requestNewNumber(numberProperty);
            sendResponseValue(res, latitude);
        }
        catch (const std::exception &e)
        {
            sendResponseValue(res, 0.0, false, std::string("Invalid request: ") + e.what());
        }
    }
    else
    {
        // GET request - return current latitude
        double latitude = 0.0;
        INDI::PropertyNumber numberProperty(geoCoord);
        for (auto &num : numberProperty)
        {
            if (num.isNameMatch("LAT"))
            {
                latitude = num.getValue();
                break;
            }
        }
        sendResponseValue(res, latitude);
    }
}

void TelescopeBridge::handleSiteLongitude(const httplib::Request &req, httplib::Response &res)
{
    auto geoCoord = m_Device.getNumber("GEOGRAPHIC_COORD");
    if (!geoCoord.isValid())
    {
        sendResponseValue(res, 0.0, false, "GEOGRAPHIC_COORD property not found");
        return;
    }

    // Handle PUT request to set site longitude
    if (req.method == "PUT")
    {
        try
        {
            // Parse form-urlencoded data from request body
            std::map<std::string, std::string> formData;
            DeviceManager::parseFormUrlEncodedBody(req.body, formData);

            // Check if SiteLongitude parameter exists
            if (formData.find("SiteLongitude") == formData.end())
            {
                sendResponseValue(res, 0.0, false, "Missing SiteLongitude parameter");
                return;
            }

            // Convert SiteLongitude value to double
            double longitude = std::stod(formData["SiteLongitude"]);

            // Update the GEOGRAPHIC_COORD property
            INDI::PropertyNumber numberProperty(geoCoord);
            for (auto &num : numberProperty)
            {
                if (num.isNameMatch("LONG"))
                    num.setValue(longitude);
            }

            // Request to send the updated property to the device
            requestNewNumber(numberProperty);
            sendResponseValue(res, longitude);
        }
        catch (const std::exception &e)
        {
            sendResponseValue(res, 0.0, false, std::string("Invalid request: ") + e.what());
        }
    }
    else
    {
        // GET request - return current longitude
        double longitude = 0.0;
        INDI::PropertyNumber numberProperty(geoCoord);
        for (auto &num : numberProperty)
        {
            if (num.isNameMatch("LONG"))
            {
                longitude = num.getValue();
                break;
            }
        }
        sendResponseValue(res, longitude);
    }
}

void TelescopeBridge::handleSiteElevation(const httplib::Request &req, httplib::Response &res)
{
    auto geoCoord = m_Device.getNumber("GEOGRAPHIC_COORD");
    if (!geoCoord.isValid())
    {
        sendResponseValue(res, 0.0, false, "GEOGRAPHIC_COORD property not found");
        return;
    }

    // Handle PUT request to set site elevation
    if (req.method == "PUT")
    {
        try
        {
            // Parse form-urlencoded data from request body
            std::map<std::string, std::string> formData;
            DeviceManager::parseFormUrlEncodedBody(req.body, formData);

            // Check if SiteElevation parameter exists
            if (formData.find("SiteElevation") == formData.end())
            {
                sendResponseValue(res, 0.0, false, "Missing SiteElevation parameter");
                return;
            }

            // Convert SiteElevation value to double
            double elevation = std::stod(formData["SiteElevation"]);

            // Update the GEOGRAPHIC_COORD property
            INDI::PropertyNumber numberProperty(geoCoord);
            for (auto &num : numberProperty)
            {
                if (num.isNameMatch("ELEV"))
                    num.setValue(elevation);
            }

            // Request to send the updated property to the device
            requestNewNumber(numberProperty);
            sendResponseValue(res, elevation);
        }
        catch (const std::exception &e)
        {
            sendResponseValue(res, 0.0, false, std::string("Invalid request: ") + e.what());
        }
    }
    else
    {
        // GET request - return current elevation
        double elevation = 0.0;
        INDI::PropertyNumber numberProperty(geoCoord);
        for (auto &num : numberProperty)
        {
            if (num.isNameMatch("ELEV"))
            {
                elevation = num.getValue();
                break;
            }
        }
        sendResponseValue(res, elevation);
    }
}
