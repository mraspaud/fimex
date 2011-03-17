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

#include "DataIndex.h"
#include "fimex/CDM.h"
#include <set>

namespace MetNoFimex
{
namespace wdb
{

DataIndex::DataIndex(const std::vector<wdb::GridData> & data)
{
	for ( std::vector<wdb::GridData>::const_iterator d = data.begin(); d != data.end(); ++ d )
		data_[d->parameter()] [d->level()] [d->version()] [d->validTo()] = d->gridIdentifier();
}

DataIndex::~DataIndex()
{
}

std::ostream & DataIndex::summary(std::ostream & s) const
{
	for ( ParameterEntry::const_iterator pe = data_.begin(); pe != data_.end(); ++ pe )
	{
		s << pe->first << '\n';
		for ( LevelEntry::const_iterator le = pe->second.begin(); le != pe->second.end(); ++ le )
		{
			s << " " << le->first << '\n';
			for ( VersionEntry::const_iterator ve = le->second.begin(); ve != le->second.end(); ++ ve )
			{
				s << "  " << ve->first << '\n';
				for ( TimeEntry::const_iterator te = ve->second.begin(); te != ve->second.end(); ++ te )
					s << "   " << te->first << ":\t" << std::showbase << std::hex << te->second << '\n';
			}
		}
	}
	std::flush(s);

	return s;
}

namespace
{
std::string toCdmName(const std::string & what)
{
	return boost::algorithm::replace_all_copy(what, " ", "_");
}
}


void DataIndex::populate(CDM & cdm) const
{
	typedef std::map<std::string, std::set<std::pair<float, float> > > LevelMap;
	// Collection of all levels in use
	LevelMap levelDimensions;

	// Highest number of dataversions in data set.
	std::size_t maxDataVersionSize = 1;

	// All times that are used in data set.
	std::set<Time> times;

	for ( ParameterEntry::const_iterator pe = data_.begin(); pe != data_.end(); ++ pe )
	{
		const std::string & parameterName = pe->first;
		const LevelEntry & levelEntry = pe->second;

		// collects names of all dimensions a particular varialbe has.
		std::vector<std::string> dimensions;

		for ( LevelEntry::const_iterator le = levelEntry.begin(); le != levelEntry.end(); ++ le )
		{
			const wdb::Level & level = le->first;
			const VersionEntry & versionEntry = le->second;


			// Find and register time dimension
			for ( VersionEntry::const_iterator ve = versionEntry.begin(); ve != versionEntry.end(); ++ ve )
			{
				const TimeEntry & timeEntry = ve->second;
				for ( TimeEntry::const_iterator te = timeEntry.begin(); te != timeEntry.end(); ++ te )
					times.insert(te->first);
				if ( timeEntry.size() > 1 and (dimensions.empty() or dimensions.front() != "time") )
					dimensions.push_back("time");
			}

			// Register data versions
			if ( versionEntry.size() > 1 )
			{
				maxDataVersionSize = std::max(maxDataVersionSize, versionEntry.size());
				dimensions.push_back("dataversion");
			}

			// Register levels
			if  (levelEntry.size() > 1 )
			{
				std::string levelName = toCdmName(level.levelName());
				levelDimensions[levelName].insert(std::make_pair(level.from(), level.to()));
				dimensions.push_back(levelName);
			}
		}
		dimensions.push_back("longitude");
		dimensions.push_back("latitude");

		CDMVariable parameter(toCdmName(parameterName), CDM_FLOAT, dimensions);
		cdm.addVariable(parameter);
//		CDMAttribute attribute()
//		cdm.addAttribute(parameter.getName(), )
	}

	for ( LevelMap::const_iterator it = levelDimensions.begin(); it != levelDimensions.end(); ++ it )
	{
		if ( it->second.size() > 1 )
		{
			CDMDimension dim(it->first, it->second.size());
			cdm.addDimension(dim);
		}
	}
	if ( maxDataVersionSize > 1 )
	{
		CDMDimension dataVersion("dataversion", maxDataVersionSize);
		cdm.addDimension(dataVersion);
	}

	cdm.addDimension(CDMDimension("longitude", 100));
	cdm.addDimension(CDMDimension("latitude", 100));

	CDMDimension time("time", times.size());
	time.setUnlimited(true);
	cdm.addDimension(time);
}


}

}