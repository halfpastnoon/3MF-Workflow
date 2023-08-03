/*++

Copyright (C) 2019 3MF Consortium

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL MICROSOFT AND/OR NETFABB BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Abstract:

Converter.cpp : Can convert 3MFs to STL and back

--*/

#include <iostream>
#include <string>
#include <algorithm>
#include <fstream>


#ifndef __GNUC__
#include <Windows.h>
#endif

#include "lib3mf_implicit.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <filesystem>
#include <OpenXLSX.hpp>

#define materials "print_materials"
#define ymod "youngs_modulus"
#define prat "poisson_ratio"
#define density "density"
#define reqs "hardware_requirements"
#define extruder_ct "extruder_count"
#define bvol "build_volume"
#define px "x"
#define py "y"
#define pz "z"

using namespace Lib3MF;
using json = nlohmann::json;
namespace fs = std::filesystem;


sColor randColor(){
	sColor ret;
	ret.m_Alpha = 255;
	ret.m_Red = rand() % 256;
	ret.m_Blue = rand() % 256;
	ret.m_Green = rand() % 256;
	return ret;
}

std::string extractMatName(std::string filename){
	std::string::size_type idx;
	idx = filename.find('_');

	if(idx != std::string::npos){
		std::string nam = filename.substr(idx+1);
		for(int i=0;i<4;i++) nam.pop_back();
		return nam;
	}
	else return "";
}

int read_matmap(std::string stl_dir, json* pticket){
	OpenXLSX::XLDocument indoc;
	try{
		indoc.open(stl_dir + "/matmap.xlsx");
	}
	catch(...){ return 0;}
	auto inwks = indoc.workbook().worksheet(indoc.workbook().sheetNames()[0]);
	int rownum = 2;
	std::string name = extractMatName(inwks.cell("A" + std::to_string(rownum)).value());
	while(!name.empty()){
		try{
			(*pticket)[materials][name][prat] = (double)inwks.cell("B" + std::to_string(rownum)).value();
		}
		catch(...){
			(*pticket)[materials][name][prat] = (int)inwks.cell("B" + std::to_string(rownum)).value();
		}
		try{
			(*pticket)[materials][name][density] = (double)inwks.cell("C" + std::to_string(rownum)).value();
		}
		catch(...){
			(*pticket)[materials][name][density] = (int)inwks.cell("C" + std::to_string(rownum)).value();
		}
		try{
			(*pticket)[materials][name][ymod] = (double)inwks.cell("D" + std::to_string(rownum)).value();
		}
		catch(...){
			(*pticket)[materials][name][ymod] = (int)inwks.cell("D" + std::to_string(rownum)).value();
		}
		rownum++;
		name = extractMatName(inwks.cell("A" + std::to_string(rownum)).value());
	}
	return rownum - 2;
}

void printVersion(PWrapper wrapper) {
	Lib3MF_uint32 nMajor, nMinor, nMicro;
	wrapper->GetLibraryVersion(nMajor, nMinor, nMicro);
	std::cout << "lib3mf version = " << nMajor << "." << nMinor << "." << nMicro;
	std::string sReleaseInfo, sBuildInfo;
	if (wrapper->GetPrereleaseInformation(sReleaseInfo)) {
		std::cout << "-" << sReleaseInfo;
	}
	if (wrapper->GetBuildInformation(sBuildInfo)) {
		std::cout << "+" << sBuildInfo;
	}
	std::cout << std::endl;
}



std::string FindExtension(std::string filename) {
	// this emulates Windows' PathFindExtension
	std::string::size_type idx;
	idx = filename.rfind('.');

	if (idx != std::string::npos)
	{
		return filename.substr(idx);
	}
	else
	{
		return "";
	}
}


int combine(std::vector<std::string> plist, std::string sOutputFilename, std::string stl_dir_path) {
	PWrapper wrapper = CWrapper::loadLibrary();
	
	std::cout << "------------------------------------------------------------------" << std::endl;
	std::cout << "3MF Model Merger" << std::endl;
	printVersion(wrapper);
	std::cout << "------------------------------------------------------------------" << std::endl;
	// std::string sReaderName = "stl";
	// std::string sWriterName = "3mf";
	// std::string sNewExtension = ".3mf";

	//create top-level model, get reader for stl and writer for 3mf
	PModel model = wrapper->CreateModel();
	PReader reader = model->QueryReader("stl");
	PWriter writer = model->QueryWriter("3mf");
	json pticket;
	pticket[reqs][extruder_ct] = plist.size();
	//pticket["materials"] = json::array();
	
	//Lib3MF::CInputVector<Lib3MF_uint8> vec;

	//add a base materials group for used materials
	PBaseMaterialGroup mat_group = model->AddBaseMaterialGroup();
	int c_res_id = 2; //iterator for the current resource id (useful when referencing read geometry)
	int matct = read_matmap(stl_dir_path, &pticket);
	if(!matct){
		std::cout << "no material map!" << std::endl;
		exit(1);
	}
	else if(matct < plist.size()){
		std::cout << "material map missing file(s)!" << std::endl;
		std::string mat_cont = (pticket[materials]).dump();
		for(int i=0;i<plist.size();i++){
			if(mat_cont.find(extractMatName(plist[i])) == std::string::npos){
				std::cout << "missing file: " << plist[i] << std::endl;
			}
		}
		exit(1);
	}
	//loop through provided stl files
	for(int i=0;i<plist.size();i++){
		std::string sFilename = plist[i];
		//acquire material name associated w this file
		std::string mat_name = extractMatName(sFilename); 
		//read geometry data from STL
		std::cout << "reading " << sFilename << "..." << std::endl;
		reader->ReadFromFile(sFilename);

		//determine material properties
		// std::cout << std::endl << "Properties of Material " << mat_name << ':' << std::endl;
		// std::cout << "Youngs Modulus:";
		// std::cin >> pticket[materials][mat_name][ymod];
		// std::cout << "Poisson's Ratio:";
		// std::cin >> pticket[materials][mat_name][prat];
		// std::cout << "Material Density:";
		// std::cin >> pticket[materials][mat_name][density];

		//add material to model file and assign to geometry that was just imported
		Lib3MF_uint32 prop_id = mat_group->AddMaterial(mat_name, randColor());
		PMeshObject just_added = model->GetMeshObjectByID(c_res_id);
		just_added->SetObjectLevelProperty(mat_group->GetResourceID(), prop_id);
		c_res_id ++;
	}
	//add print ticket to 3mf package
	std::cout << "writing print ticket..." << std::endl;

	sBox bbox = model->GetOutbox();
	Lib3MF_single x = bbox.m_MaxCoordinate[0] - bbox.m_MinCoordinate[0];
	Lib3MF_single y = bbox.m_MaxCoordinate[1] - bbox.m_MinCoordinate[1];
	Lib3MF_single z = bbox.m_MaxCoordinate[2] - bbox.m_MinCoordinate[2];
	pticket[reqs][bvol][px] = x;
	pticket[reqs][bvol][py] = y;
	pticket[reqs][bvol][pz] = z;

	//std::ofstream file("pticket.json");
	//file << std::setw(4) << pticket << std::endl;
	PAttachment pattach = model->AddAttachment("/3D/Metadata/Model_PT.json", "http://schemas.openxmlformats.org/package/2006/relationships/metadata/thumbnail");
	std::string fstring = pticket.dump(4);
	std::vector<Lib3MF_uint8> pbuf(fstring.begin(), fstring.end());
	pattach->ReadFromBuffer(pbuf);
	//pattach->ReadFromFile("pticket.json");
	//remove("pticket.json");
	//compile and write final 3mf package
	std::cout << "writing " << sOutputFilename << "..." << std::endl;
	writer->WriteToFile(stl_dir_path + "/" + sOutputFilename);
	std::cout << "done" << std::endl;



	return 0;
}

std::string to_wsl_dir(std::string windir){
	windir.erase(std::remove(windir.begin(), windir.end(), '\"'), windir.end());
	std::replace(windir.begin(), windir.end(), '\\', '/');
	std::string drivename = windir.substr(0, 1);
	std::transform(drivename.begin(), drivename.end(), drivename.begin(),
	[](unsigned char c){ return std::tolower(c); });
	return "/mnt/" + drivename + windir.substr(2);
}

int main(int argc, char** argv) {
	// Parse Arguments
	std::string out_name = argv[2];
	std::string out_ext = FindExtension(out_name);
	std::vector<std::string> plist;
	if(out_ext.compare(".3mf")){
		std::cout << "Last File must be .3mf output file" << std::endl;
		return 0;
	}
	std::string stl_dir_path = argv[1];
	stl_dir_path = to_wsl_dir(stl_dir_path);
	std::string str_cont;
	for(const auto &entry : fs::directory_iterator(stl_dir_path)){
		str_cont = entry.path();
		if(str_cont.find(".stl") != std::string::npos) plist.push_back(str_cont);
	}
	try {
		return combine(plist, out_name, stl_dir_path);
	}
	catch (ELib3MFException &e) {
		std::cout << e.what() << std::endl;
		return e.getErrorCode();
	}
	return 0;
}
