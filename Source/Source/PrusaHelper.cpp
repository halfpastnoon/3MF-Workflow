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
#include <map>
#include <cmath>

#ifndef __GNUC__
#include <Windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#endif

#include "lib3mf_implicit.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include "inipp.h"
#include <stdio.h>
#include <stdlib.h>

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

PWrapper wrapper = CWrapper::loadLibrary();

bool dcomp(double x, double y, double err = 0.0001f){
	return fabs(x - y) < err ? true : false;
}

sColor randColor(){
	sColor ret;
	ret.m_Alpha = 255;
	ret.m_Red = rand() % 256;
	ret.m_Blue = rand() % 256;
	ret.m_Green = rand() % 256;
	return ret;
}

std::string get_dir(std::string path){
	return path.substr(0, path.rfind("/")+1);
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

//TODO: if cant open file, error out
std::vector<std::string> parse_3mf(json *pticket, std::string filename){
    
	std::vector<std::string> ret;
    std::cout << "parsing 3MF..." << std::endl;
    PModel model = wrapper->CreateModel();
    PReader reader = model->QueryReader("3mf");
    reader->ReadFromFile(filename);
    PAttachment pattach = model->FindAttachment("/3D/Metadata/Model_PT.json");
    std::vector<Lib3MF_uint8> pbuf;
    pattach->WriteToBuffer(pbuf);
    *pticket = json::parse(pbuf);
	PBaseMaterialGroup mat_group = model->GetBaseMaterialGroupByID(1);
	std::vector<Lib3MF_uint32> pids;
	mat_group->GetAllPropertyIDs(pids);
	for(Lib3MF_uint32 i=0;i<pids.size();i++) ret.push_back(mat_group->GetName(pids[i]));
	model.reset();
	return ret;
}

std::vector<std::string> get_extruder_matlist(std::string main_ini_path){
	std::vector<std::string> ret;
	inipp::Ini<char> main_ini;
	std::ifstream is(main_ini_path);
	main_ini.parse(is);
	std::string fil_key = "filament";
	std::string filler;
	int filct = 0;
	while(true){
		inipp::get_value(main_ini.sections["presets"], fil_key, filler);
		if(!filler.empty()){
			filct++;
			ret.push_back(filler);
			filler.clear();
			fil_key = "filament_" + std::to_string(filct);
		}
		else return ret;
	}	
}

std::vector<double> parse_fil_notes(std::string notes_buf){
	std::vector<double> ret(3);
	std::string delim = "\\n";
	int idx = notes_buf.rfind(delim);
	std::vector<std::string> splitter;
	splitter.push_back(notes_buf.substr(1, notes_buf.find(delim)-1));
	splitter.push_back(notes_buf.substr(notes_buf.find(delim)+2, idx - notes_buf.find(delim)-2));
	splitter.push_back(notes_buf.substr(idx+2, notes_buf.size()-1 - idx - 2));
	for(int i=0;i<splitter.size();i++){
		if(splitter[i].find(prat) != std::string::npos){
			ret[0] = std::stod(splitter[i].substr(splitter[i].find(":")+1)); 
		}
		else if(splitter[i].find(density) != std::string::npos){
			ret[1] = std::stod(splitter[i].substr(splitter[i].find(":")+1));
		}
		else if(splitter[i].find(ymod) != std::string::npos){
			ret[2] = std::stod(splitter[i].substr(splitter[i].find(":")+1));
		}
	}
	return ret;
}

std::vector<std::vector<double>> get_condensed_props(std::string filaments_path, std::vector<std::string> extruder_matlist){
	std::string fil_key;
	std::string notes_buf;
	inipp::Ini<char> fil_ini;
	std::vector<std::vector<double>> ret;
	for(int i=0;i<extruder_matlist.size();i++){
		fil_key = filaments_path + "/" + extruder_matlist[i] + ".ini";
		std::ifstream is(fil_key);
		fil_ini.parse(is);
		is.close();
		inipp::get_value(fil_ini.sections[""] ,"filament_notes", notes_buf);
		ret.push_back(parse_fil_notes(notes_buf));
		notes_buf.clear();
		fil_ini.clear();
	}
	return ret;
	
}


//TODO: if cant open file, error out
std::vector<int> loop_read_config(std::string pdir, json pticket, std::vector<std::string> order){
	std::cout << "parsing PrintTicket..." << std::endl;
	std::vector< std::map<std::string, double> > props(order.size()*3*sizeof(double)*500*sizeof(char)); //i cry
	std::vector<int> ret;
	std::stringstream save_stream;
	int match_ct = 0;
	for(int i=0;i<order.size();i++){
		props[i][density] = pticket[materials][order[i]][density];
		props[i][prat] = pticket[materials][order[i]][prat];
		props[i][ymod] = pticket[materials][order[i]][ymod];
	}
	
	std::string main_ini_path = pdir;
	main_ini_path.append("/PrusaSlicer.ini");
	std::string filaments_path = pdir;
	filaments_path.append("/filament");
	do{ //if you are reading this, i am sorry
		std::cout << "parsing PrusaSlicer configuration files..." << std::endl;
		std::vector<std::string> extruder_matlist = get_extruder_matlist(main_ini_path);
		std::vector<std::vector<double>> extruder_ordlist = get_condensed_props(filaments_path, extruder_matlist);
		for(int i=0;i<order.size();i++){ 
			int ext_num = -1; 
			bool flag = true;
			for(int j=0;j<extruder_matlist.size();j++){
				if (dcomp(extruder_ordlist[j][0], props[i][prat]) && dcomp(extruder_ordlist[j][1], props[i][density]) && dcomp(extruder_ordlist[j][2], props[i][ymod])){
					match_ct++;
					ret.push_back(j+1);
					flag = false;
					break;
				} 
			}
			if(flag){
				std::stringstream stream;
				stream << std::endl << "missing the material with the following properties:" << std::endl;
				stream << "Poisson's Ratio: " << props[i][prat] << std::endl;
				stream << "Material Density: " << props[i][density] << std::endl;
				stream << "Young's Modulus: " << props[i][ymod] << std::endl;
				if(save_stream.str().find(stream.str()) == std::string::npos){
					std::cout << stream.str();
					save_stream << stream.str();
				}
				flag = false;
			}
		}
		if(match_ct < order.size()){
			save_stream.str(std::string());
			ret.clear();
			std::cout << "required filaments not loaded, please update PrusaSlicer and press 'enter' key" << std::endl;
			std::getchar();
		}
	}while(match_ct < order.size());
	return ret;
}

// std::string convert_to_prusa_3mf(std::string tfname){
// 	std::cout << "creating PrusaSlicer project file..." << std::endl;
// 	std::string command = "prusa-slicer --export-3mf " + tfname + " | grep \"File exported to\"";
// 	FILE* prusa_pipe = popen(command.c_str(), "r");
// 	char str_buf[1024];
// 	fgets(str_buf, 1024, prusa_pipe);
// 	std::string outstr(str_buf);
// 	std::string newfname = outstr.substr(outstr.find("/"), outstr.find("\n") - outstr.find("/"));
// 	fclose(prusa_pipe);
// 	return newfname;
// }

// void modify_prusa_proj(std::vector<int> extruder_map, std::string tfname, sigset_t* sigset, int* sig){
// 	std::cout << "assigning materials..." << std::endl;
// 	std::system(("unzip -qq " + tfname + " -d pFileDir").c_str());
// 	std::system(("rm " + tfname).c_str());
// 	std::string config_loc = "pFileDir/Metadata/Slic3r_PE_model.config";
// 	std::string err;
// 	XMLFile* config_fil = ::OpenXMLFile(config_loc, err);
// 	if(!config_fil){
// 		std::cout << err << std::endl;
// 		exit(1);
// 	}
// 	XMLDocument* config_dom = ::CreateXMLFromFile(config_fil, err);
// 	if(!config_dom){
// 		std::cout << err << std::endl;
// 		::DisposeXMLFile(config_fil);
// 		exit(1);
// 	}
	
// 	::SetHeader(config_dom, 1, "UTF-8", err);
// 	XMLElement* top = ::FirstOrDefaultElement(config_dom, "config", err);
// 	if(!top){
// 		std::cout << err << std::endl;
// 		::DisposeXMLObject(config_dom);
// 		::DisposeXMLFile(config_fil);
// 		exit(1);
// 	}
	
// 	XMLElement* sib_it = top->first_node("object");
// 	if(!sib_it){
// 		//std::cout << "here" << std::endl;
// 		std::cout << err << std::endl;
// 		exit(1);
// 	}
// 	int fid = 0;
// 	for(int i=0;i<extruder_map.size();i++){
// 		XMLElement* new_mdata_element = ::CreateElement(config_dom, "metadata", "", err);
// 		if(!new_mdata_element){
// 			std::cout << err << std::endl;
// 			exit(1);
// 		}
// 		XMLAttributte* type_attr = ::CreateAttribute(config_dom, "type", "volume", err);
// 		if(!type_attr){
// 			std::cout << err << std::endl;
// 			exit(1);
// 		}
// 		XMLAttributte* key_attr = ::CreateAttribute(config_dom, "key", "extruder", err);
// 		if(!key_attr){
// 			std::cout << err << std::endl;
// 			exit(1);
// 		}
// 		XMLAttributte* value_attr = ::CreateAttribute(config_dom, "value", std::to_string(extruder_map[i]), err);
// 		if(!value_attr){
// 			std::cout << err << std::endl;
// 			exit(1);
// 		}
// 		if(!::AddAttributeToElement(new_mdata_element, type_attr, err)){
// 			std::cout << err << std::endl;
// 			exit(1);
// 		}
// 		if(!::AddAttributeToElement(new_mdata_element, key_attr, err)){
// 			std::cout << err << std::endl;
// 			exit(1);
// 		}
// 		if(!::AddAttributeToElement(new_mdata_element, value_attr, err)){
// 			std::cout << err << std::endl;
// 			exit(1);
// 		}
// 		XMLElement* vol_cont = sib_it->first_node("volume");
// 		if(!vol_cont){
// 			std::cout << err << std::endl;
// 			exit(1);
// 		}
// 		XMLAttributte* fid_attr = vol_cont->first_attribute("firstid");
// 		if(!::SetAttributeValue(fid_attr, std::to_string(fid), err)){
// 			std::cout << err << std::endl;
// 			exit(1);
// 		}
// 		XMLAttributte* lid_attr = vol_cont->first_attribute("lastid");

// 		vol_cont->append_node(new_mdata_element);
// 		sib_it = sib_it->next_sibling("object");
// 		// if(!sib_it){
// 		// 	std::cout << err << std::endl;
// 		// 	exit(1);
// 		// }
// 		//append new node to sib_it
// 		//get next sibling, put in sib it
// 	}
	
// 	if(!::SaveXML(*config_dom, config_loc)){
// 		std::cout << "could not create file" << std::endl;
// 	}

// 	std::string command = ("cd pFileDir && zip -r -q ../" + tfname.substr(tfname.rfind("/")+1) + " .");
// 	std::system(command.c_str());
// 	std::system("rm -r pFileDir");
// 	std::cout << "waiting for PrusaSlicer..." << std::endl;
// 	sigwait(sigset, sig);
// 	//std::getchar();
// 	std::system(("powershell.exe prusa-slicer-console.exe --single-instance " + tfname.substr(tfname.rfind("/")+1) + " > /dev/null").c_str());
// }

std::string to_wsl_dir(std::string windir){
	windir.erase(std::remove(windir.begin(), windir.end(), '\"'), windir.end());
	std::replace(windir.begin(), windir.end(), '\\', '/');
	std::string drivename = windir.substr(0, 1);
	std::transform(drivename.begin(), drivename.end(), drivename.begin(),
	[](unsigned char c){ return std::tolower(c); });
	return "/mnt/" + drivename + windir.substr(2);
}

int main(int argc, char** argv) {
	
	
	//args: 3mf file, where is prusa dir
	int sig;
	sigset_t sigset;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGUSR1);
	sigprocmask(SIG_BLOCK, &sigset, NULL);
	std::string tfname_win = argv[1]; //"\"C:\\Users\\Noone\\Desktop\\TestFolder\\thefile.3mf\"";
	std::string tfname = to_wsl_dir(tfname_win);
	std::string tfdir = get_dir(tfname);
	//tfname_win.erase(std::remove(tfname_win.begin(), tfname_win.end(), '\"'), tfname_win.end());

	std::string windir = argv[2]; //"\"C:\\Users\\Noone\\AppData\\Roaming\\PrusaSlicer\"";
	std::string pdir = to_wsl_dir(windir);//"/mnt/" + drivename + windir.substr(2); //"/home/mnoon/.config/PrusaSlicer";
	if(!fork()){
		FILE* cmd_pipe = popen("powershell.exe prusa-slicer-console.exe --loglevel 3 --single-instance", "r");
		
		char buf[1024];
		for(int i=0;i<25;i++)fgets(buf, 1024, cmd_pipe);
		kill(getppid(), SIGUSR1);
		fclose(cmd_pipe);
		//std::system("powershell.exe prusa-slicer-console.exe > /dev/null");
		exit(0);
	}

	
	try {
        json pticket;
		std::vector<std::string> order = parse_3mf(&pticket, tfname);
		std::string main_ini_path = pdir;
		std::vector<int> extruder_map = loop_read_config(pdir, pticket, order);
		std::cout << "writing extruder map..." << std::endl;
		std::ofstream exs((tfdir + "extruder_map.dat"), std::ios::binary);
		int exm_sz = extruder_map.size();
		exs.write((const char *)&exm_sz, sizeof(int));
		exs.write((const char *)&extruder_map[0], exm_sz * sizeof(int));
		exs.close();
		std::cout << "waiting for PrusaSlicer..." << std::endl;
		sigwait(&sigset, &sig);
		std::string cmd = "powershell.exe prusa-slicer-console.exe --single-instance \"" + tfname_win + "\" > /dev/null";
		std::system(cmd.c_str());
		


		//std::string tfname_new = convert_to_prusa_3mf(tfname);
		
		//modify_prusa_proj(extruder_map, tfname_new, &sigset, &sig);
		std::cout << "done" << std::endl;
		wait(NULL);
		return 0;
		//Read 3MF file (pticket and order) into internal structures
		//Read prusa files into internal structures and error check loop until printing can occur
	}
	catch (ELib3MFException &e) {
		std::cout << e.what() << std::endl;
		return e.getErrorCode();
	}
	return 0;
}
