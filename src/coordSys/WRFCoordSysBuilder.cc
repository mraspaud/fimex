/*
 * Fimex, WRFCoordSysBuilder.cc
 *
 * (C) Copyright 2012, met.no
 *
 * Project Info:  https://wiki.met.no/fimex/start
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 *
 *  Created on: Mar 28, 2012
 *      Author: Heiko Klein
 */

#include "WRFCoordSysBuilder.h"
#include "fimex/coordSys/CoordinateSystem.h"
#include "fimex/CDM.h"
#include "fimex/CDMReader.h"
#include "fimex/CDMAttribute.h"
#include "fimex/Data.h"
#include "fimex/mifi_constants.h"
#include "fimex/Logger.h"
#include "fimex/interpolation.h"
#include "proj_api.h"
#include <cmath>
#include <vector>
#include <map>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace MetNoFimex
{

WRFCoordSysBuilder::WRFCoordSysBuilder()
{
}

WRFCoordSysBuilder::~WRFCoordSysBuilder()
{
}

bool WRFCoordSysBuilder::isMine(const CDM& cdm)
{
    if (!cdm.hasDimension("south_north"))
        return false;

    CDMAttribute attr;
    if (!cdm.getAttribute(cdm.globalAttributeNS(), "MAP_PROJ", attr))
        return false;

    return true;
}

static double findAttributeDouble(const CDM& cdm, std::string attName)
{
    CDMAttribute attr;
    if (!cdm.getAttribute(cdm.globalAttributeNS(), attName, attr))
        return MIFI_UNDEFINED_D;
    else
        return attr.getData()->asDouble()[0];

}

// function for compatibility between the old (only CDM) and new (only CDMReader) interface
static std::vector<boost::shared_ptr<const CoordinateSystem> > wrfListCoordinateSystems(CDM& cdm, boost::shared_ptr<CDMReader> reader)
{
    // reader might be 0
    using namespace std;
    LoggerPtr logger = getLogger("fimex.coordSys.WRFCoordSysBuilder");
    std::vector<boost::shared_ptr<const CoordinateSystem> > coordSys;
    if (!WRFCoordSysBuilder().isMine(cdm))
        return coordSys;
    /*
    The definition for map projection options:
    map_proj =  1: Lambert Conformal
                2: Polar Stereographic
                3: Mercator
                6: latitude and longitude (including global)
     */
    double lat1 = findAttributeDouble(cdm, "TRUELAT1");
    double lat2 = findAttributeDouble(cdm, "TRUELAT2");
    double centralLat = findAttributeDouble(cdm, "CEN_LAT");  // center of grid
    double centralLon = findAttributeDouble(cdm, "CEN_LON");  // center of grid
    double standardLon = findAttributeDouble(cdm, "STAND_LON"); // true longitude
    double standardLat = findAttributeDouble(cdm, "MOAD_CEN_LAT");

    std::stringstream proj4;
    int map_proj = cdm.getAttribute(cdm.globalAttributeNS(), "MAP_PROJ").getData()->asInt()[0];
    switch (map_proj) {
    case 1:
        proj4 << "+proj=lcc +lon_0="<<standardLon<<" +lat_0="<<standardLat<<" +lat_1="<<lat1<<" +lat_2="<<lat2;
        break;
    case 2:
        proj4 << "+proj=stere";
        {
            double lon0 = (mifi_isnand(standardLon)) ? centralLon : standardLon;
            double lat0 = (mifi_isnand(centralLat)) ? lat2 : centralLat;
            double k = (1+fabs(sin(DEG_TO_RAD*lat1)))/2.;
            proj4 << " +lon_0="<<lon0<<" +lat_0="<<lat0<<" +k="<<k;
        }
        break;
    case 3:
        proj4 << "+proj=merc +lon_0="<<standardLon<<" +lat_0="<<standardLat;
        break;
    default:
        LOG4FIMEX(logger, Logger::WARN, "unknown projection-id: " << map_proj);
        return coordSys;
    }
    proj4 << " +R=6370000 +no_defs"; // WRF earth radius = 6370km
    boost::shared_ptr<Projection> proj = Projection::createByProj4(proj4.str());
    double centerX = centralLon * DEG_TO_RAD;
    double centerY = centralLat * DEG_TO_RAD;
    mifi_project_values(MIFI_WGS84_LATLON_PROJ4, proj4.str().c_str(), &centerX, &centerY, 1);
    // build axes and staggered coordinate system
    CoordinateSystem::AxisPtr timeAxis, westAxis, stagWestAxis, northAxis, stagNorthAxis, bottomAxis, stagBottomAxis;
    if (cdm.hasDimension("west_east")) {
        vector<string> shape;
        shape.push_back("west_east");
        if (!cdm.hasVariable(shape.at(0))) {
            int dx = cdm.getAttribute(cdm.globalAttributeNS(), "DX").getData()->asInt()[0];
            size_t dimSize = cdm.getDimension(shape.at(0)).getLength();
            double startX = centerX - dx * (dimSize - 1) / 2;
            boost::shared_array<float> vals(new float[dimSize]);
            for (size_t i = 0; i < dimSize; i++) {
                vals[i] = startX + i * dx;
            }
            cdm.addVariable(CDMVariable(shape.at(0),CDM_FLOAT, shape));
            cdm.addAttribute(shape.at(0), CDMAttribute("units", "m"));
            cdm.addAttribute(shape.at(0), CDMAttribute("standard_name", "projection_x_axis"));
            cdm.getVariable(shape.at(0)).setData(createData(dimSize, vals));
        }
        westAxis = CoordinateSystem::AxisPtr(new CoordinateAxis(cdm.getVariable(shape.at(0))));
        westAxis->setAxisType(CoordinateAxis::GeoX);
        westAxis->setExplicit(true);
    }
    if (cdm.hasDimension("west_east_stag")) {
        vector<string> shape;
        shape.push_back("west_east_stag");
        if (!cdm.hasVariable(shape.at(0))) {
            int dx = cdm.getAttribute(cdm.globalAttributeNS(), "DX").getData()->asInt()[0];
            size_t dimSize = cdm.getDimension(shape.at(0)).getLength();
            double startX = centerX - dx * (dimSize - 1) / 2;
            boost::shared_array<float> vals(new float[dimSize]);
            for (size_t i = 0; i < dimSize; i++) {
                vals[i] = startX + i * dx;
            }
            cdm.addVariable(CDMVariable(shape.at(0),CDM_FLOAT, shape));
            cdm.addAttribute(shape.at(0), CDMAttribute("units", "m"));
            cdm.addAttribute(shape.at(0), CDMAttribute("standard_name", "projection_x_axis"));
            cdm.getVariable(shape.at(0)).setData(createData(dimSize, vals));
        }
        stagWestAxis = CoordinateSystem::AxisPtr(new CoordinateAxis(cdm.getVariable(shape.at(0))));
        stagWestAxis->setAxisType(CoordinateAxis::GeoX);
        stagWestAxis->setExplicit(true);
    }
    if (cdm.hasDimension("south_north")) {
        vector<string> shape;
        shape.push_back("south_north");
        if (!cdm.hasVariable(shape.at(0))) {
            int dy =
                    cdm.getAttribute(cdm.globalAttributeNS(), "DY").getData()->asInt()[0];
            size_t dimSize = cdm.getDimension(shape.at(0)).getLength();
            double startY = centerY - dy * (dimSize - 1) / 2;
            boost::shared_array<float> vals(new float[dimSize]);
            for (size_t i = 0; i < dimSize; i++) {
                vals[i] = startY + i * dy;
            }
            cdm.addVariable(CDMVariable(shape.at(0), CDM_FLOAT, shape));
            cdm.addAttribute(shape.at(0), CDMAttribute("units", "m"));
            cdm.addAttribute(shape.at(0),
                    CDMAttribute("standard_name", "projection_y_axis"));
            cdm.getVariable(shape.at(0)).setData(createData(dimSize, vals));
        }
        northAxis = CoordinateSystem::AxisPtr(new CoordinateAxis(cdm.getVariable(shape.at(0))));
        northAxis->setAxisType(CoordinateAxis::GeoY);
        northAxis->setExplicit(true);
    }
    if (cdm.hasDimension("south_north_stag")) {
        vector<string> shape;
        shape.push_back("south_north_stag");
        if (!cdm.hasVariable(shape.at(0))) {
            int dy =
                    cdm.getAttribute(cdm.globalAttributeNS(), "DY").getData()->asInt()[0];
            size_t dimSize = cdm.getDimension(shape.at(0)).getLength();
            double startY = centerY - dy * (dimSize - 1) / 2;
            boost::shared_array<float> vals(new float[dimSize]);
            for (size_t i = 0; i < dimSize; i++) {
                vals[i] = startY + i * dy;
            }
            cdm.addVariable(CDMVariable(shape.at(0), CDM_FLOAT, shape));
            cdm.addAttribute(shape.at(0), CDMAttribute("units", "m"));
            cdm.addAttribute(shape.at(0),
                    CDMAttribute("standard_name", "projection_y_axis"));
            cdm.getVariable(shape.at(0)).setData(createData(dimSize, vals));
        }
        stagNorthAxis = CoordinateSystem::AxisPtr(new CoordinateAxis(cdm.getVariable(shape.at(0))));
        stagNorthAxis->setAxisType(CoordinateAxis::GeoY);
        stagNorthAxis->setExplicit(true);
    }
    if (cdm.hasDimension("bottom_top")) {
        vector<string> shape;
        shape.push_back("bottom_top");
        if (!cdm.hasVariable(shape.at(0))) {
            size_t dimSize = cdm.getDimension(shape.at(0)).getLength();
            boost::shared_array<float> vals(new float[dimSize]);
            for (size_t i = 0; i < dimSize; i++) {
                vals[i] = i;
            }
            cdm.addVariable(CDMVariable(shape.at(0), CDM_FLOAT, shape));
            cdm.addAttribute(shape.at(0), CDMAttribute("units", "1"));
            cdm.getVariable(shape.at(0)).setData(createData(dimSize, vals));
        }
        bottomAxis = CoordinateSystem::AxisPtr(new CoordinateAxis(cdm.getVariable(shape.at(0))));
        bottomAxis->setAxisType(CoordinateAxis::GeoZ);
        bottomAxis->setExplicit(true);
    }
    if (cdm.hasDimension("bottom_top_stag")) {
        vector<string> shape;
        shape.push_back("bottom_top_stag");
        if (!cdm.hasVariable(shape.at(0))) {
            size_t dimSize = cdm.getDimension(shape.at(0)).getLength();
            boost::shared_array<float> vals(new float[dimSize]);
            for (size_t i = 0; i < dimSize; i++) {
                vals[i] = i;
            }
            cdm.addVariable(CDMVariable(shape.at(0), CDM_FLOAT, shape));
            cdm.addAttribute(shape.at(0), CDMAttribute("units", "1"));
            cdm.getVariable(shape.at(0)).setData(createData(dimSize, vals));
        }
        stagBottomAxis = CoordinateSystem::AxisPtr(new CoordinateAxis(cdm.getVariable(shape.at(0))));
        stagBottomAxis->setAxisType(CoordinateAxis::GeoZ);
        stagBottomAxis->setExplicit(true);
    }

    // determine the Time
    if (cdm.hasDimension("Time") && !cdm.hasVariable("Time")) {
        vector<string> shape;
        shape.push_back("Time");
        if (!cdm.hasVariable(shape.at(0))) {
            string units;
            size_t dimSize = cdm.getDimension(shape.at(0)).getLength();
            boost::shared_array<float> vals(new float[dimSize]);
            if (reader.get() == 0) {
                // try to guess the time-steps
                string start = cdm.getAttribute(cdm.globalAttributeNS(),
                        "START_DATE").getStringValue();
                vector<string> datetime = tokenize(start, "_");
                units = "hours since " + datetime.at(0) + " "
                        + datetime.at(1) + " +0000";

                float timeStep = 180.;
                CDMAttribute timeStepAttr;
                if (cdm.getAttribute(cdm.globalAttributeNS(), "TIME_STEP_MN",
                        timeStepAttr)) {
                    // attribute from ncml
                    timeStep = timeStepAttr.getData()->asFloat()[0];
                } else {
                    LOG4FIMEX(logger, Logger::WARN,
                            "Could not find attribute TIME_STEP_MN, guessing 180minutes time-steps");
                }
                timeStep /= 60;
                for (size_t i = 0; i < dimSize; i++) {
                    vals[i] = timeStep * i;
                }
            } else {
                // try to read the time from the data-reader TIMES-variable
                // Times is usually given as 'YYYY-MM-DD_HH:MM:SS' in UTM as string
                SliceBuilder sb(cdm, "Times");
                size_t timeSize = cdm.getDimension("Time").getLength();
                boost::posix_time::ptime ptFirst;
                for (size_t i = 0; i < timeSize; ++i) {
                    sb.setStartAndSize("Time", i, 1);
                    boost::shared_ptr<Data> data = reader->getDataSlice("Times", sb);
                    // define units and set time in that unit as float to vals
                    string thisTime = data->asString();
                    if (thisTime.find("_") != string::npos) {
                        thisTime = thisTime.replace(thisTime.find("_"), 1, " "); // replace _ with space
                    }
                    boost::posix_time::ptime pt(boost::posix_time::time_from_string(thisTime));
                    if (i == 0) {
                        ptFirst = pt;
                        boost::posix_time::time_facet* time_output = new boost::posix_time::time_facet("hours since %Y-%m-%d %H:%M:%S");
                        stringstream ss;
                        ss.imbue(std::locale(ss.getloc(), time_output));
                        ss << ptFirst;
                        units = ss.str();
                    }
                    boost::posix_time::time_duration td(pt - ptFirst);
                    vals[i] = td.hours() + td.minutes()/60. + td.seconds()/3600.;
                }
            }
            cdm.addVariable(CDMVariable(shape.at(0), CDM_FLOAT, shape));
            cdm.addAttribute(shape.at(0), CDMAttribute("units", units));
            cdm.getVariable(shape.at(0)).setData(createData(dimSize, vals));
        }
        timeAxis = CoordinateSystem::AxisPtr(
                new CoordinateAxis(cdm.getVariable(shape.at(0))));
        timeAxis->setAxisType(CoordinateAxis::Time);
        timeAxis->setExplicit(true);
        // ref-time
        string reftime = "forecast_reference_time";
        if (!cdm.hasVariable(reftime)) {
            cdm.addVariable(CDMVariable(reftime, CDM_INT, vector<string>(0)));
            string ref = cdm.getAttribute(cdm.globalAttributeNS(),
                    "SIMULATION_START_DATE").getStringValue();
            vector<string> refdatetime = tokenize(ref, "_");
            string refunits = "hours since " + refdatetime.at(0) + " "
                    + refdatetime.at(1) + " +0000";
            cdm.addAttribute(reftime, CDMAttribute("units", refunits));
            cdm.addAttribute(reftime, CDMAttribute("standard_name", reftime));
            boost::shared_array<float> refvals(new float[1]);
            refvals[0] = 0;
            cdm.getVariable(reftime).setData(createData(1, refvals));
        }
    }

    // now, create the coordinate-systems depending on the variables shape
    map<string, boost::shared_ptr<CoordinateSystem> > cs;
    const CDM::VarVec vars = cdm.getVariables();
    for (CDM::VarVec::const_iterator vit = vars.begin(); vit != vars.end(); ++vit) {
        vector<string> shape = vit->getShape();
        // only adding CoordinateSystems for x y * systems
        if (((find(shape.begin(), shape.end(), "west_east") != shape.end()) || (find(shape.begin(), shape.end(), "west_east_stag") != shape.end()))
            && ((find(shape.begin(), shape.end(), "south_north") != shape.end()) || (find(shape.begin(), shape.end(), "south_north_stag") != shape.end()))) {
            string shapeId = join(shape.begin(), shape.end());
            if (cs.find(shapeId) == cs.end()) {
                boost::shared_ptr<CoordinateSystem> coord(new CoordinateSystem(WRFCoordSysBuilder().getName()));
                coord->setProjection(proj);
                for (vector<string>::iterator dimIt = shape.begin(); dimIt != shape.end(); dimIt++) {
                    if (*dimIt == "west_east") {
                        coord->setAxis(westAxis);
                    } else if (*dimIt == "west_east_stag") {
                        coord->setAxis(stagWestAxis);
                    } else if (*dimIt == "south_north") {
                        coord->setAxis(northAxis);
                    } else if (*dimIt == "south_north_stag") {
                        coord->setAxis(stagNorthAxis);
                    } else if (*dimIt == "Time") {
                        coord->setAxis(timeAxis);
                    } else if (*dimIt == "bottom_top") {
                        coord->setAxis(bottomAxis);
                    } else if (*dimIt == "bottom_top_stag") {
                        coord->setAxis(stagBottomAxis);
                    } else {
                        if (!cdm.hasVariable(*dimIt)) {
                            // add a dimension without a variable with a 'virtual' variable
                            vector<string> myshape(1, *dimIt);
                            cdm.addVariable(CDMVariable(*dimIt, CDM_INT, myshape));
                            size_t dimSize = cdm.getDimension(*dimIt).getLength();
                            boost::shared_array<float> vals(new float[dimSize]);
                            for (size_t i = 0; i < dimSize; i++) {
                                vals[i] = i;
                            }
                            cdm.getVariable(*dimIt).setData(createData(dimSize, vals));
                        }
                        CoordinateSystem::AxisPtr other = CoordinateSystem::AxisPtr(new CoordinateAxis(cdm.getVariable(*dimIt)));
                        other->setExplicit(true);
                        coord->setAxis(other);
                    }
                }
                coord->setSimpleSpatialGridded(true);
                cs[shapeId] = coord;
            }
            boost::shared_ptr<CoordinateSystem> coord = cs[shapeId];
            coord->setCSFor(vit->getName(), true);
            coord->setComplete(vit->getName(), true);
        }
    }
    for (map<string, boost::shared_ptr<CoordinateSystem> >::iterator csIt = cs.begin(); csIt != cs.end(); ++csIt) {
        coordSys.push_back(csIt->second);
    }


    return coordSys;
}
std::vector<boost::shared_ptr<const CoordinateSystem> > WRFCoordSysBuilder::listCoordinateSystems(CDM& cdm)
{
    return wrfListCoordinateSystems(cdm, boost::shared_ptr<CDMReader>());
}

std::vector<boost::shared_ptr<const CoordinateSystem> > WRFCoordSysBuilder::listCoordinateSystems(boost::shared_ptr<CDMReader> reader)
{
    return wrfListCoordinateSystems(reader->getInternalCDM(), reader);
}

#if 0
    // do the tasks which don't require the reader (compatibility with older interface
    std::vector<boost::shared_ptr<const CoordinateSystem> > cs = listCoordinateSystems(reader->getInternalCDM());

    // read the Time information (ASCII times) and add them to the dimension-variable 'Time'
    CDM& cdm = reader->getInternalCDM();
    if (cdm.hasDimension("Time") && (!cdm.hasVariable("Time")) && cdm.hasVariable("Times")) {
        std::cerr << "I am here now";
        // Times is usually given as 'YYYY-MM-DD_HH:MM:SS' in UTM as string
        SliceBuilder sb(cdm, "Times");
        size_t timeSize = cdm.getDimension("Time").getLength();
        for (size_t i = 0; i < timeSize; ++i) {
            sb.setStartAndSize("Time", i, 1);
            boost::shared_ptr<Data> data = reader->getDataSlice("Times", sb);
            std::cerr << data->asString() << std::endl;
        }
    }

    return cs;
}

#endif
} /* namespace MetNoFimex */