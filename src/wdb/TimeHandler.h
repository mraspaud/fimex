/*
 fimex

 Copyright (C) 2011 met.no

 Contact information:
 Norwegian Meteorological Institute
 Box 43 Blindern
 0313 OSLO
 NORWAY
 E-mail: post@met.no

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 MA  02110-1301, USA
 */

#ifndef TIMEHANDLER_H_
#define TIMEHANDLER_H_

#include "DataHandler.h"
#include "database_access/GridData.h"

namespace MetNoFimex
{
class CDM;
class Data;

namespace wdb
{
class WdbIndex;


/**
 * Handling time for wdb/fimex. Objects of this class may be used to add time
 * dimensions and -variables to CDM objects, and to get time values for the
 * same variables.
 */
class TimeHandler : public DataHandler
{
public:
	/**
	 * Times will be extracted and calculated from the given index object
	 */
	explicit TimeHandler(const WdbIndex & index);
	virtual ~TimeHandler();

	/**
	 * Add relevant time dimensions and -variables to the given CDM object.
	 */
	virtual void addToCdm(CDM & cdm) const;

	/**
	 * Get data for the given variable name and unlimited dimension
	 */
	virtual DataPtr getData(const CDMVariable & variable, size_t unLimDimPos) const;

	/**
	 * Does the given cdm variable name refer to anything that this object can
	 * handle via the getData method?
	 */
	virtual bool canHandle(const std::string & cfName) const;

	virtual void addToCoordinatesAttribute(std::string & coordinates, const std::string & wdbName) const;

	enum Dimension
	{
		ReferenceTime, ValidTime, Other
	};
	Dimension unlimitedDimension() const;

	static const std::string referenceTimeName;
	static const std::string validTimeName;
	static const std::string timeOffsetName;

private:
	GridData::Time getReferenceTime(size_t unLimDimPos) const;

	void addReferenceTimeToCdm(CDM & cdm) const;
	void addTimeOffsetToCdm(CDM & cdm) const;
	void addValidTimeToCdm(CDM & cdm) const;

	const WdbIndex & index_;
};

}
}

#endif /* TIMEHANDLER_H_ */
