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

//extracts print ticket data from 3MF package
//returns list of material names in order of appearance in .model file
//parameter pticket: print ticket json extracted from 3MF package
//parameter filename: absolute WSL path to input 3MF package 
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

//reads PrusaSlicer.ini and returns list of PrusaSlicer filament names in order of extruder id
//parameter main_ini_path: absolute WSL path to PrusaSlicer.ini
std::vector<std::string> get_extruder_matlist(std::string main_ini_path){
	std::vector<std::string> ret;
	inipp::Ini<char> main_ini;
	std::ifstream is(main_ini_path);
	if(is.fail()){
		std::cout << "could not open " << main_ini_path << "!" << std::endl;
		exit(1); 
	}
	main_ini.parse(is);
	is.close();
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

//reads filaments_notes string from the INI file describing the material 
//returns vector of doubles; 0:Poisson's Ratio, 1:Material Density, 2:Young's Modulus
//parameter notes_buf: buffer containing filament_notes field as string, comes from filament INI file
std::vector<double> parse_fil_notes(std::string notes_buf){
	std::vector<double> ret(3);
	std::string delim = "\\n";
	int idx = notes_buf.rfind(delim);
	std::vector<std::string> splitter;
	splitter.push_back(notes_buf.substr(1, notes_buf.find(delim)-1));
	splitter.push_back(notes_buf.substr(idx+2, notes_buf.size()-1 - idx - 2));
	for(int i=0;i<splitter.size();i++){
		if(splitter[i].find(prat) != std::string::npos){
			ret[0] = std::stod(splitter[i].substr(splitter[i].find(":")+1)); 
		}
		else if(splitter[i].find(ymod) != std::string::npos){
			ret[2] = std::stod(splitter[i].substr(splitter[i].find(":")+1));
		}
	}
	return ret;
}

//returns a list in order of extruder id containing material properties of materials assigned to each extruder
//parameter filaments_path: absolute WSL path to filament folder within PrusaSlicer AppData folder
//parameter extruder_matlist: string vector containing list of material names ordered by extruder id (output of get_extruder_matlist)
std::vector<std::vector<double>> get_condensed_props(std::string filaments_path, std::vector<std::string> extruder_matlist){
	std::string fil_key;
	std::string notes_buf;
	inipp::Ini<char> fil_ini;
	std::vector<std::vector<double>> ret;
	for(int i=0;i<extruder_matlist.size();i++){
		fil_key = filaments_path + "/" + extruder_matlist[i] + ".ini";
		std::ifstream is(fil_key);
		if(is.fail()){
			std::cout << "could not open " << fil_key << "!" << std::endl;
			exit(1); 
		}
		fil_ini.parse(is);
		is.close();
		inipp::get_value(fil_ini.sections[""] ,"filament_notes", notes_buf);
		std::vector<double> ret_push = parse_fil_notes(notes_buf);
		inipp::get_value(fil_ini.sections[""], "filament_density", ret_push[1]);
		ret.push_back(ret_push);
		notes_buf.clear();
		fil_ini.clear();
	}
	return ret;
	
}

//reads PrusaSlicer config files and compares to provided print ticket. If PrusaSlicer has all the right materials, open the file. Otherwise, notify user and wait for update.
//returns integer vector of extruder ids ordered by the geometry to which they map (e.g. if first object in model file is vertebrae, first extruder id has a material similar to bone)
//parameter pdir: absolute WSL path to PrusaSlicer AppData folder
//parameter pticket: print ticket json extracted from 3MF package
//parameter order: string vecor of material names ordered by presence in .model file
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

//returns WSL-accessible directory given a directory from Windows
//parameter windir: directory path from windows containing quotes (usually output from 'Copy As Path' in windows)
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
	std::string tfname_win = argv[1]; 
	std::string tfname = to_wsl_dir(tfname_win);
	std::string tfdir = get_dir(tfname);


	std::string windir = argv[2]; 
	std::string pdir = to_wsl_dir(windir);
	if(!fork()){
		FILE* cmd_pipe = popen("powershell.exe prusa-slicer-console.exe --loglevel 3 --single-instance", "r");
		
		char buf[1024];
		for(int i=0;i<25;i++)fgets(buf, 1024, cmd_pipe);
		kill(getppid(), SIGUSR1);
		fclose(cmd_pipe);
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
