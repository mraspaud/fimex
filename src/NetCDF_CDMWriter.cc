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

#include "fimex/NetCDF_CDMWriter.h"
#include "../config.h"
#include "netcdfcpp.h"
extern "C" {
#include "netcdf.h"             // the C interface
}

#include <iostream>
#include <boost/shared_array.hpp>
#include "fimex/mifi_constants.h"
#include "fimex/CDMDataType.h"
#include "fimex/DataTypeChanger.h"
#include "NetCDF_Utils.h"
#include "fimex/Units.h"
#include "fimex/Utils.h"
#include "fimex/XMLDoc.h"
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include "fimex/Logger.h"
#include "fimex/NcmlCDMReader.h"

namespace MetNoFimex
{

static LoggerPtr logger = getLogger("fimex.NetCDF_CDMWriter");


static NcBool putRecData(NcVar* var, CDMDataType dt, boost::shared_ptr<Data> data, size_t recNum) {
	if (data->size() == 0) return true;

	NcDim* dim = var->get_dim(0); // 0 dimension must be record dimension (unlimited if any)
	var->set_rec(dim, recNum);
	switch (dt) {
	case CDM_NAT: return false;
	case CDM_CHAR: return var->put_rec(dim, reinterpret_cast<const ncbyte*>(data->asConstChar().get()));
	case CDM_STRING: return var->put_rec(dim, data->asConstChar().get());
	case CDM_SHORT:  return var->put_rec(dim, data->asConstShort().get());
	case CDM_INT: return var->put_rec(dim, data->asConstInt().get());
	case CDM_FLOAT: return var->put_rec(dim, data->asConstFloat().get());
	case CDM_DOUBLE: return var->put_rec(dim, data->asConstDouble().get());
	default: return false;
	}

}


static NcBool putVarData(NcVar* var, CDMDataType dt, boost::shared_ptr<Data> data) {
	size_t size = data->size();
	if (size == 0) return true;

	boost::shared_array<long> edges(var->edges());
	int dims = var->num_dims();
	int dim_size = 1;
	for (int i = 0; i < dims; i++) {
		dim_size *= edges[i];
	}
	if (size != static_cast<size_t>(dim_size)) {
		return false;
	}

	switch (dt) {
	case CDM_NAT: return false;
	case CDM_CHAR:   return var->put(reinterpret_cast<const ncbyte*>(data->asChar().get()),  edges.get());
	case CDM_STRING: return var->put(data->asConstChar().get(),  edges.get());
	case CDM_SHORT:  return var->put(data->asConstShort().get(), edges.get());
	case CDM_INT:    return var->put(data->asConstInt().get(),   edges.get());
	case CDM_FLOAT:  return var->put(data->asConstFloat().get(), edges.get());
	case CDM_DOUBLE: return var->put(data->asConstDouble().get(),edges.get());
	default: return false;
	}

}

#ifdef HAVE_NCFILE_FILEFORMAT
NcFile::FileFormat getNcVersion(int version, std::auto_ptr<XMLDoc>& doc)
{
	NcFile::FileFormat retVal = NcFile::Classic;
	switch (version) {
		case 3: retVal = NcFile::Classic; break;
#ifdef NC_NETCDF4
		case 4: retVal = NcFile::Netcdf4; break;
#endif
		default: LOG4FIMEX(logger, Logger::ERROR, "unknown netcdf-version "<< version << " using 3 instead");
	}
	if (doc.get() != 0) {
		// set the default filetype
		XPathObjPtr xpathObj = doc->getXPathObject("/cdm_ncwriter_config/default[@filetype]");
		xmlNodeSetPtr nodes = xpathObj->nodesetval;
		if (nodes->nodeNr) {
			std::string filetype = string2lowerCase(getXmlProp(nodes->nodeTab[0], "filetype"));
			if (filetype == "netcdf3") {
				retVal = NcFile::Classic;
			} else if (filetype == "netcdf3_64bit") {
				retVal = NcFile::Offset64Bits;
			}
#ifdef NC_NETCDF4
			else if (filetype == "netcdf4") {
				retVal = NcFile::Netcdf4;
			} else if (filetype == "netcdf4classic") {
				retVal = NcFile::Netcdf4Classic;
			}
#endif
			else {
				throw CDMException("unknown netcdf-filetype: " + filetype);
			}
		}
	}
	return retVal;
}
#endif /* HAVE_NCFILE_FILEFORMAT */

NetCDF_CDMWriter::NetCDF_CDMWriter(boost::shared_ptr<CDMReader> cdmReader, const std::string& outputFile, std::string configFile, int version)
: CDMWriter(cdmReader, outputFile)
{
	LoggerPtr logger = getLogger("fimex.NetCDF_CDMWriter");
	std::auto_ptr<XMLDoc> doc;
	if (configFile == "") {
		doc = std::auto_ptr<XMLDoc>(0);
	} else {
		doc = std::auto_ptr<XMLDoc>(new XMLDoc(configFile));
	}
	ncErr = std::auto_ptr<NcError>(new NcError(NcError::verbose_nonfatal));
#ifdef HAVE_NCFILE_FILEFORMAT
	NcFile::FileFormat ncVersion = getNcVersion(version, doc);
	ncFile = std::auto_ptr<NcFile>(new NcFile(outputFile.c_str(), NcFile::Replace, 0, 0, ncVersion));
	switch (ncFile->get_format()) {
		case NcFile::Classic: LOG4FIMEX(logger, Logger::DEBUG, "classic format, version: "); break;
#ifdef NC_NETCDF4
		case NcFile::Netcdf4: LOG4FIMEX(logger, Logger::DEBUG, "netcdf4 format, version: "); break;
#endif
		default: LOG4FIMEX(logger, Logger::DEBUG, "format: " << ncFile->get_format());
	}
#else
    ncFile = std::auto_ptr<NcFile>(new NcFile(outputFile.c_str(), NcFile::Replace));
#endif /* HAVE_NCFILE_FILEFORMAT */
    if (!ncFile->is_valid()) {
        // ncErr.get_err does not work in new NcFile, try to get the error message manually
        size_t *bufrsizeptr = new size_t;
        int the_id;
        int error = nc__create(outputFile.c_str(), NC_WRITE|NC_NOCLOBBER, 0, bufrsizeptr, &the_id);
        throw CDMException(nc_strerror(error));
        //throw CDMException(nc_strerror(ncErr.get_err()));
    }
    initNcmlReader(doc);
	initRemove(doc);
	// variable needs to be called before dimension!!!
	initFillRenameVariable(doc);
	initFillRenameDimension(doc);
	initFillRenameAttribute(doc);

	init();

}

void NetCDF_CDMWriter::initNcmlReader(std::auto_ptr<XMLDoc>& doc) throw(CDMException)
{
    if (doc.get() != 0) {
        XPathObjPtr xpathObj = doc->getXPathObject("/cdm_ncwriter_config/ncmlConfig");
        xmlNodeSetPtr nodes = xpathObj->nodesetval;
        int size = (nodes) ? nodes->nodeNr : 0;
        if (size > 0) {
            std::string configFile = getXmlProp(nodes->nodeTab[0], "filename");
            LOG4FIMEX(logger, Logger::DEBUG, "configuring CDMWriter with ncml config file: " << configFile);
            cdmReader = boost::shared_ptr<CDMReader>(new NcmlCDMReader(cdmReader, configFile));
        }
    }
    cdm = cdmReader->getCDM();
}

void NetCDF_CDMWriter::initRemove(std::auto_ptr<XMLDoc>& doc) throw(CDMException)
{
	if (doc.get() != 0) {
		{
			// remove global attributes
			XPathObjPtr xpathObj = doc->getXPathObject("/cdm_ncwriter_config/remove[@type='attribute']");
			xmlNodeSetPtr nodes = xpathObj->nodesetval;
			int size = (nodes) ? nodes->nodeNr : 0;
			for (int i = 0; i < size; i++) {
				std::string name = getXmlProp(nodes->nodeTab[i], "name");
				cdm.removeAttribute(CDM::globalAttributeNS(), name);
			}
		}
		{
			// remove variable attributes
			XPathObjPtr xpathObj = doc->getXPathObject("/cdm_ncwriter_config/variable/remove[@type='attribute']");
			xmlNodeSetPtr nodes = xpathObj->nodesetval;
			int size = (nodes) ? nodes->nodeNr : 0;
			for (int i = 0; i < size; i++) {
				std::string name = getXmlProp(nodes->nodeTab[i], "name");
				std::string varName = getXmlProp(nodes->nodeTab[i]->parent, "name");
				cdm.removeAttribute(varName, name);
			}
		}
		{
			// remove variables
			XPathObjPtr xpathObj = doc->getXPathObject("/cdm_ncwriter_config/remove[@type='variable']");
			xmlNodeSetPtr nodes = xpathObj->nodesetval;
			int size = (nodes) ? nodes->nodeNr : 0;
			for (int i = 0; i < size; i++) {
		        LOG4FIMEX(logger, Logger::WARN, "Removing variables in cdmWriterConfig.xml is deprecated, use ncmlCDMConfig instead!");
				std::string name = getXmlProp(nodes->nodeTab[i], "name");
				cdm.removeVariable(name);
			}
		}

	}
}

void NetCDF_CDMWriter::initFillRenameDimension(std::auto_ptr<XMLDoc>& doc) throw(CDMException)
{
	if (doc.get() != 0) {
		XPathObjPtr xpathObj = doc->getXPathObject("/cdm_ncwriter_config/dimension[@newname]");
		xmlNodeSetPtr nodes = xpathObj->nodesetval;
		int size = (nodes) ? nodes->nodeNr : 0;
		for (int i = 0; i < size; i++) {
		    LOG4FIMEX(logger, Logger::WARN, "Changing renaming dimensions in cdmWriterConfig.xml is deprecated, use ncmlCDMConfig instead!");
			std::string name = getXmlProp(nodes->nodeTab[i], "name");
			std::string newname = getXmlProp(nodes->nodeTab[i], "newname");
			cdm.getDimension(name); // check existence, throw exception
			dimensionNameChanges[name] = newname;
			// change dimension variable unless it has been changed
			if (variableNameChanges.find(name) == variableNameChanges.end()) {
				variableNameChanges[name] = newname;
			}
		}
	}
}

void NetCDF_CDMWriter::testVariableExists(const std::string& varName) throw(CDMException)
{
	try {
		cdm.getVariable(varName);
	} catch (CDMException& e) {
		throw CDMException(std::string("error modifying variable in writer: ") + e.what());
	}
}

void NetCDF_CDMWriter::initFillRenameVariable(std::auto_ptr<XMLDoc>& doc) throw(CDMException)
{
	if (doc.get() != 0) {
		XPathObjPtr xpathObj = doc->getXPathObject("/cdm_ncwriter_config/variable[@newname]");
		xmlNodeSetPtr nodes = xpathObj->nodesetval;
		int size = (nodes) ? nodes->nodeNr : 0;
		for (int i = 0; i < size; i++) {
		    LOG4FIMEX(logger, Logger::WARN, "Changing variable-names in cdmWriterConfig.xml is deprecated, use ncmlCDMConfig instead!");
			std::string name = getXmlProp(nodes->nodeTab[i], "name");
			testVariableExists(name);
			std::string newname = getXmlProp(nodes->nodeTab[i], "newname");
			variableNameChanges[name] = newname;
		}
	}
	if (doc.get() != 0) {
		// read 'type' attribute and enable re-typeing of data
		XPathObjPtr xpathObj = doc->getXPathObject("/cdm_ncwriter_config/variable[@type]");
		xmlNodeSetPtr nodes = xpathObj->nodesetval;
		int size = (nodes) ? nodes->nodeNr : 0;
		for (int i = 0; i < size; i++) {
			std::string name = getXmlProp(nodes->nodeTab[i], "name");
			CDMDataType type = string2datatype(getXmlProp(nodes->nodeTab[i], "type"));
			variableTypeChanges[name] = type;
		}
	}
	unsigned int defaultCompression = 3;
	if (doc.get() != 0) {
		// set the default compression level for all variables
		XPathObjPtr xpathObj = doc->getXPathObject("/cdm_ncwriter_config/default[@compressionLevel]");
		xmlNodeSetPtr nodes = xpathObj->nodesetval;
		if (nodes->nodeNr) {
			defaultCompression = string2type<unsigned int>(getXmlProp(nodes->nodeTab[0], "compressionLevel"));
		}
	}
	const CDM::VarVec& vars = cdm.getVariables();
	for (CDM::VarVec::const_iterator varIt = vars.begin(); varIt != vars.end(); ++varIt) {
		variableCompression[varIt->getName()] = defaultCompression;
	}
	if (doc.get() != 0) {
		// set the compression level for all variables
		XPathObjPtr xpathObj = doc->getXPathObject("/cdm_ncwriter_config/variable[@compressionLevel]");
		xmlNodeSetPtr nodes = xpathObj->nodesetval;
		int size = (nodes) ? nodes->nodeNr : 0;
		for (int i = 0; i < size; i++) {
			std::string name = getXmlProp(nodes->nodeTab[i], "name");
			unsigned int compression = string2type<unsigned int>(getXmlProp(nodes->nodeTab[0], "compressionLevel"));
			variableCompression[name] = compression;
		}
	}

}

void NetCDF_CDMWriter::initFillRenameAttribute(std::auto_ptr<XMLDoc>& doc) throw(CDMException)
{
	// make a complete copy of the original attributes
	if (doc.get() != 0) {
	XPathObjPtr xpathObj = doc->getXPathObject("//attribute");
	xmlNodeSetPtr nodes = xpathObj->nodesetval;
	int size = (nodes) ? nodes->nodeNr : 0;
	for (int i = 0; i < size; i++) {
		xmlNodePtr node = nodes->nodeTab[i];
		std::string attName = getXmlProp(node, "name");
		if (attName != "units") LOG4FIMEX(logger, Logger::WARN, "Changing attributes in cdmWriterConfig.xml is deprecated, use ncmlCDMConfig instead!");
		std::string varName = CDM::globalAttributeNS();
		xmlNodePtr parent = node->parent;
		std::string parentName = getXmlName(parent);
		if (parentName == "cdm_ncwriter_config") {
			// default
		} else if (parentName == "variable") {
			varName = getXmlProp(parent, "name");
			testVariableExists(varName);
		} else {
			throw CDMException("unknown parent of attribute "+attName+": "+parentName);
		}

		std::string attValue = getXmlProp(node, "value");
		std::string attType = getXmlProp(node, "type");
		std::string attNewName = getXmlProp(node, "newname");
		std::string newAttrName = attNewName != "" ? attNewName : attName;
		CDMAttribute attr;
		if (attType != "") {
			attr = CDMAttribute(newAttrName, attType, attValue);
		} else {
			const CDMAttribute& oldAttr = cdm.getAttribute(varName, attName);
			attr = CDMAttribute(newAttrName, oldAttr.getDataType(), oldAttr.getData());
		}
		cdm.removeAttribute(varName, attName); // remove the attribute with the old name
		cdm.addAttribute(varName, attr); // set the attribute with the new name and data
	}
	}
}


NetCDF_CDMWriter::NcDimMap NetCDF_CDMWriter::defineDimensions() {
	const CDM::DimVec& cdmDims = cdm.getDimensions();
	NcDimMap ncDimMap;
	for (CDM::DimVec::const_iterator it = cdmDims.begin(); it != cdmDims.end(); ++it) {
		int length = it->isUnlimited() ? NC_UNLIMITED : it->getLength();
		// NcDim is organized by NcFile, no need to clean
		// change the name written to the file according to getDimensionName
		NcDim* dim = ncFile->add_dim(getDimensionName(it->getName()).c_str(), length);
		if (dim == 0) throw CDMException(nc_strerror(ncErr->get_err()));
		ncDimMap[it->getName()] = dim;
	}
	return ncDimMap;
}

NetCDF_CDMWriter::NcVarMap NetCDF_CDMWriter::defineVariables(const NcDimMap& ncDimMap) {
	LoggerPtr logger = getLogger("fimex.NetCDF_CDMWriter");
	const CDM::VarVec& cdmVars = cdm.getVariables();
	NcVarMap ncVarMap;
	for (CDM::VarVec::const_iterator it = cdmVars.begin(); it != cdmVars.end(); ++it) {
		const CDMVariable& var = *it;
		const std::vector<std::string>& shape = var.getShape();
		// the const-ness required by the library
		typedef const NcDim* NcDimPtr;
		boost::shared_array<NcDimPtr> ncshape(new NcDimPtr[shape.size()]);
		for (size_t i = 0; i < shape.size(); i++) {
			// revert order, cdm requires fastest moving first, netcdf-cplusplus requires fastest moving first
			ncshape[i] = ncDimMap.find(shape[(shape.size()-1-i)])->second;
		}
		CDMDataType datatype = var.getDataType();
		if (variableTypeChanges.find(var.getName()) != variableTypeChanges.end()) {
			CDMDataType& newType = variableTypeChanges[var.getName()];
			datatype = newType != CDM_NAT ? newType : datatype;
		}
		if (datatype == CDM_NAT && shape.size() == 0) {
			// empty variable, use int datatype
			datatype = CDM_INT;
		}
		NcVar *ncVar = ncFile->add_var(getVariableName(var.getName()).c_str(), cdmDataType2ncType(datatype), shape.size(), ncshape.get());
		if (! ncVar) {
			throw CDMException(nc_strerror(ncErr->get_err()));
		}
#ifdef NC_NETCDF4
		// set compression
		if (ncFile->get_format() == NcFile::Netcdf4) {
			int compression = 0;
			assert(variableCompression.find(var.getName()) != variableCompression.end());
			if (variableCompression.find(var.getName()) != variableCompression.end()) {
				compression = variableCompression[var.getName()];
			}
			if (compression > 0 &&  shape.size() >= 1) { // non-scalar variables
				LOG4FIMEX(logger, Logger::DEBUG, "compressing variable " << var.getName() << " with level " << compression);
				int ncerr = nc_def_var_deflate(ncFile->id(), ncVar->id(), 0, 1, compression);
				if (ncerr != NC_NOERR) {
					throw CDMException(nc_strerror(ncerr));
				}
			}
		}
#endif
		ncVarMap[var.getName()] = ncVar;
	}
	return ncVarMap;
}

void NetCDF_CDMWriter::writeAttributes(const NcVarMap& ncVarMap) {
	// using C interface since it offers a combined interface to global and var attributes
	const CDM::StrAttrVecMap& attributes = cdm.getAttributes();
	for (CDM::StrAttrVecMap::const_iterator it = attributes.begin(); it != attributes.end(); ++it) {
		int varId;
		if (it->first == CDM::globalAttributeNS()) {
			varId = NC_GLOBAL;
		} else {
			varId = ncVarMap.find(it->first)->second->id();
		}
		for (CDM::AttrVec::const_iterator ait = it->second.begin(); ait != it->second.end(); ++ait) {
			int errCode = NC_NOERR;
			const CDMAttribute& attr = *ait;
			CDMDataType dt = attr.getDataType();
			switch (dt) {
			case CDM_STRING: ;
                errCode = nc_put_att_text(ncFile->id(), varId, attr.getName().c_str(), attr.getData()->size(), attr.getData()->asConstChar().get() );
                break;
			case CDM_CHAR:
			    errCode = nc_put_att_schar(ncFile->id(), varId, attr.getName().c_str(), static_cast<nc_type>(cdmDataType2ncType(dt)), attr.getData()->size(), reinterpret_cast<const signed char*>(attr.getData()->asConstChar().get()) );
				break;
			case CDM_SHORT:
				errCode = nc_put_att_short(ncFile->id(), varId, attr.getName().c_str(), static_cast<nc_type>(cdmDataType2ncType(dt)), attr.getData()->size(), attr.getData()->asConstShort().get() );
				break;
			case CDM_INT:
				errCode = nc_put_att_int(ncFile->id(), varId, attr.getName().c_str(), static_cast<nc_type>(cdmDataType2ncType(dt)), attr.getData()->size(), attr.getData()->asConstInt().get() );
				break;
			case CDM_FLOAT:
				errCode = nc_put_att_float(ncFile->id(), varId, attr.getName().c_str(), static_cast<nc_type>(cdmDataType2ncType(dt)), attr.getData()->size(), attr.getData()->asConstFloat().get() );
				break;
			case CDM_DOUBLE:
				errCode = nc_put_att_double(ncFile->id(), varId, attr.getName().c_str(), static_cast<nc_type>(cdmDataType2ncType(dt)), attr.getData()->size(), attr.getData()->asConstDouble().get() );
				break;
			case CDM_NAT:
			default: throw CDMException("unknown datatype for attribute " + attr.getName());
			}
			if (errCode != NC_NOERR) {
			    std::cerr << "attribute writing error: " << it->first << " " << varId << std::endl;
				throw CDMException(nc_strerror(errCode));
			}
		}
	}
}

double NetCDF_CDMWriter::getOldAttribute(const std::string& varName, const std::string& attName, double defaultValue) const
{
	double retVal = defaultValue;
	try {
		const CDMAttribute& attr = cdmReader->getCDM().getAttribute(varName, attName);
		retVal = attr.getData()->asDouble()[0];
	} catch (CDMException& e) {} // don't care
	return retVal;
}
double NetCDF_CDMWriter::getNewAttribute(const std::string& varName, const std::string& attName, double defaultValue) const
{
	double retVal = defaultValue;
	try {
		const CDMAttribute& attr = getAttribute(varName, attName);
		retVal = attr.getData()->asDouble()[0];
	} catch (CDMException& e) {} // don't care
	return retVal;
}


void NetCDF_CDMWriter::writeData(const NcVarMap& ncVarMap) {
	Units units;
	const CDM::VarVec& cdmVars = cdm.getVariables();
	for (CDM::VarVec::const_iterator it = cdmVars.begin(); it != cdmVars.end(); ++it) {
		const CDMVariable& cdmVar = *it;
		const std::string& varName = cdmVar.getName();
		DataTypeChanger dtc(cdmVar.getDataType());
		if ((variableTypeChanges.find(varName) != variableTypeChanges.end()) &&
			(variableTypeChanges[varName] != CDM_NAT)) {
			double oldFill = getOldAttribute(varName, "_FillValue", MIFI_UNDEFINED_D);
			double oldScale = getOldAttribute(varName, "scale_factor", 1.);
			double oldOffset = getOldAttribute(varName, "add_offset", 0.);
			double newFill = getNewAttribute(varName, "_FillValue", MIFI_UNDEFINED_D);
			double newScale = getNewAttribute(varName, "scale_factor", 1.);
			double newOffset = getNewAttribute(varName, "add_offset", 0.);

			// changes of the units
			double unitSlope = 1.;
			double unitOffset = 0.;
			std::string oldUnit;
			std::string newUnit;
			try {
				oldUnit = cdmReader->getCDM().getAttribute(varName, "units").getData()->asString();
				newUnit = getAttribute(varName, "units").getData()->asString();
				if (oldUnit != newUnit) {
					units.convert(oldUnit, newUnit, unitSlope, unitOffset);
				}
			} catch (UnitException& e) {
				std::cerr << "Warning: unable to convert data-units for variable " << cdmVar.getName() << ": " << e.what() << std::endl;
			} catch (CDMException& e) {
				// units not defined, do nothing
			}

			dtc = DataTypeChanger(cdmVar.getDataType(), oldFill, oldScale, oldOffset, variableTypeChanges[cdmVar.getName()], newFill, newScale, newOffset, unitSlope, unitOffset);
		}
		NcVar* ncVar = ncVarMap.find(cdmVar.getName())->second;
		if (!cdm.hasUnlimitedDim(cdmVar)) {
			boost::shared_ptr<Data> data = cdmReader->getData(cdmVar.getName());
			try {
				data = dtc.convertData(data);
			} catch (CDMException& e) {
				throw CDMException("problems converting data of var " + cdmVar.getName() + ": " + e.what());
			}
			if (!putVarData(ncVar, dtc.getDataType(), data)) {
				throw CDMException("problems writing data to var " + cdmVar.getName() + ": " + nc_strerror(ncErr->get_err()) + ", datalength: " + type2string(data->size()));
			}
		} else {
			// iterate over each unlimited dim (usually time)
			const CDMDimension* unLimDim = cdm.getUnlimitedDim();
			for (size_t i = 0; i < unLimDim->getLength(); ++i) {
				boost::shared_ptr<Data> data = cdmReader->getDataSlice(cdmVar.getName(), i);
				try {
					data = dtc.convertData(data);
				} catch (CDMException& e) {
					throw CDMException("problems converting data of var " + cdmVar.getName() + ": " + e.what());
				}
				if (!putRecData(ncVar, dtc.getDataType(), data, i)) {
					throw CDMException("problems writing datarecord " + type2string(i) + " to var " + cdmVar.getName() + ": " + nc_strerror(ncErr->get_err()) + ", datalength: " + type2string(data->size()));
				}
			}
		}
	}
}

void NetCDF_CDMWriter::init() throw(CDMException)
{
	// write metadata
	if (! ncFile->is_valid()) {
		throw CDMException(nc_strerror(ncErr->get_err()));
	}
	NcDimMap ncDimMap = defineDimensions();
	NcVarMap ncVarMap = defineVariables(ncDimMap);
	writeAttributes(ncVarMap);
	writeData(ncVarMap);
}

NetCDF_CDMWriter::~NetCDF_CDMWriter()
{
}

const CDMAttribute& NetCDF_CDMWriter::getAttribute(const std::string& varName, const std::string& attName) const throw(CDMException)
{
	return cdm.getAttribute(varName, attName);
}

const std::string& NetCDF_CDMWriter::getVariableName(const std::string& varName) const
{
	if (variableNameChanges.find(varName) == variableNameChanges.end()) {
		return varName;
	} else {
		return variableNameChanges.find(varName)->second;
	}
}
const std::string& NetCDF_CDMWriter::getDimensionName(const std::string& dimName) const
{
	if (dimensionNameChanges.find(dimName) == dimensionNameChanges.end()) {
		return dimName;
	} else {
		return dimensionNameChanges.find(dimName)->second;
	}
}

}
