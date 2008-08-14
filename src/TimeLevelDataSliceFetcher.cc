/*
 * Fimex
 *
 * (C) Copyright 2008, met.no
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
 */

#include "fimex/TimeLevelDataSliceFetcher.h"

namespace MetNoFimex
{

TimeLevelDataSliceFetcher::~TimeLevelDataSliceFetcher()
{
}

TimeLevelDataSliceFetcher::TimeLevelDataSliceFetcher(boost::shared_ptr<CDMReader> cdmReader, const std::string& varName)
: cdmReader(cdmReader), varName(varName)
{
	const CDM& cdm = cdmReader->getCDM();
	std::string time = cdm.getTimeAxis(varName);
	std::string level = cdm.getVerticalAxis(varName);
	std::string unLimDim;
	const CDMDimension* unLim = cdm.getUnlimitedDim();
	if (unLim != 0) {
		unLimDim = unLim->getName();
	}
	std::vector<std::string> shape = cdm.getVariable(varName).getShape();
	timePos = -1;
	levelPos = -1;
	unLimPos = -1;
	for (size_t i = 0; i < shape.size(); i++) {
		const CDMDimension& dim = cdm.getDimension(shape[i]);
		orgShape.push_back(dim.getLength());
		if (shape[i] == time) timePos = i;
		else if (shape[i] == level) levelPos = i;
		if (shape[i] == unLimDim) unLimPos = i;
	}
	if (levelPos == -1) {
		levelPos = orgShape.size();
		orgShape.push_back(1);
	}
	if (timePos == -1) {
		timePos = orgShape.size();
		orgShape.push_back(1);
	}
}
boost::shared_ptr<Data> TimeLevelDataSliceFetcher::getTimeLevelSlice(size_t time, size_t level) throw(CDMException) {
	std::cerr << "getTimeLevelSlice: " << time << " " << level << " " << timePos << " " << levelPos << " " << unLimPos << std::endl;
	// setting the shape of the input and output-data
	std::vector<size_t> finalShape = orgShape;
	std::vector<size_t> startDims(orgShape.size(), 0);
	finalShape[levelPos] = 1;
	finalShape[timePos] = 1;
	startDims[levelPos] = level;
	startDims[timePos] = time;
	// adjusting the shape according to the unlimited position
	if (unLimPos == timePos) {
		startDims[timePos] = 0;
		orgShape[timePos] = 1;
		if ((dataCache.get() == 0) || (time != dataCachePos)) {
			dataCache = cdmReader->getDataSlice(varName, time);
			dataCachePos = time;
		}
	} else if (unLimPos == levelPos) {
		startDims[levelPos] = 0;
		orgShape[levelPos] = 1;
		if ((dataCache.get() == 0) || (level != dataCachePos)) {
			dataCache = cdmReader->getDataSlice(varName, timePos);
			dataCachePos = level;
		}
	} else {
		if (dataCache.get() == 0) {
			// get all data, joined already
			dataCache = cdmReader->getData(varName);
		}
	}
	if (dataCache->size() == 0) {
		return createData(CDM_DOUBLE, 0);
	}
	boost::shared_ptr<Data> data = dataCache->slice(orgShape, startDims, finalShape);
	return data;
}


}
