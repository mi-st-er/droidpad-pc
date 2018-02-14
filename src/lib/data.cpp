/*
 * This file is part of DroidPad.
 * DroidPad lets you use an Android mobile to control a joystick or mouse
 * on a Windows or Linux computer.
 *
 * DroidPad is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * DroidPad is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with DroidPad, in the file COPYING.
 * If not, see <http://www.gnu.org/licenses/>.
 */
#include "data.hpp"

#include <fstream>
#include <vector>
#include <sstream>

#include <wx/stdpaths.h>
#include <wx/textfile.h>
#include <wx/tokenzr.h>
#include <wx/filename.h>
#include <wx/intl.h>
#include <wx/utils.h>

#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int_distribution.hpp>

#include "log.hpp"

using namespace std;
using namespace droidpad;

wxString Data::datadir = wxT("");
wxString Data::confLocation = wxT("");
wxString Data::host = wxT("");
wxString Data::computerName = wxT("");
wxChar Data::blackKey = 'b';
wxChar Data::whiteKey = 'w';
boost::uuids::uuid Data::computerUuid;
bool Data::secureSupported = false;
#ifdef DEBUG
bool Data::noAdb = false;
#endif

wxConfig *Data::config = NULL;

Tweaks Data::tweaks = Tweaks();

wxString Data::version = wxT(VERSION);

vector<Credentials> CredentialStore::credentials;
boost::random::mt19937 CredentialStore::gen;
boost::uuids::random_generator CredentialStore::uuidGen(gen);

// This class now uses wxConfig. Any other code is simply for compatibility

// REMEMBER: Update these if more buttons / axes added in future
const int initialButtons[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
			      16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
vector<int> Data::buttonOrder = vector<int>(initialButtons, initialButtons + 32);
const int initialAxes[] = {0, 1, 2, 3, 4, 5, 6, 7, -1, -1, -1}; // 8 axes + 3 additional for reordering (-1 for reorder to none)
vector<int> Data::axisOrder = vector<int>(initialAxes, initialAxes + 11);

int Data::port = 3141;

#define CONF_FILE "dp.conf"

bool Data::initialise()
{
	// Seed random gen. Currently using time, need to find a better source of entropy.
	CredentialStore::gen.seed(std::time(0));

	std::vector<wxString> datadirs;
	wxString testFile = wxT("layout.xrc");

	// TODO: make this code better, to use app directory rather than cwd

	datadirs.push_back(wxString(wxT("") PREFIX "/share/" PACKAGE_NAME));
	datadirs.push_back(wxStandardPaths::Get().GetPluginsDir() + wxT("\\data"));
	datadirs.push_back(wxT("data"));
	datadirs.push_back(wxT("../data"));
	datadirs.push_back(wxT("."));

	//string dataDir;

	for(int i = 0; i < datadirs.size(); i++)
	{
		LOGVwx(wxString(wxT("Trying folder ")) + datadirs[i]);
		ifstream tstream((datadirs[i] + wxT("/") + testFile).mb_str());
		if(tstream)
		{
			LOGVwx(wxString::Format(wxT("Found data folder @ \"%s\"."), datadirs[i].c_str()));
			datadir = datadirs[i];
			break;
		}
	}
	if(datadir == wxT(""))
	{
		LOGE("ERROR: Could not find data!");
		return false;
	}

	// Initialise to default first
	tweaks = createDefaultTweaks();
	computerName = wxString::Format(_("%s's Computer"), wxGetUserName().c_str()).Mid(0, 40);
	computerUuid = CredentialStore::uuidGen();

	// Attempt to open new wxConfig format
	config = new wxConfig(wxT("droidpad"), wxT("digitalsquid"));
	wxString tmp;
	if(config->Read(wxT("initialised"), &tmp)) {
		// Read from wxConfig
		LOGV("Reading new preferences format");
		loadPreferences();
		return true;
	}
	LOGV("Reading old preferences format and converting");
	// Else, continue to read from old format

	confLocation = wxStandardPaths::Get().GetUserDataDir();

	wxTextFile config(wxString(confLocation.c_str(), wxConvUTF8) + wxT("/") + wxT(CONF_FILE));

	if(config.Open())
	{ // Process config file
		for(wxString line = config.GetFirstLine(); !config.Eof(); line = config.GetNextLine())
		{
			wxStringTokenizer tkz(line, wxT(";"));
			if(!tkz.HasMoreTokens()) continue; // Malformed line
			wxString key = tkz.GetNextToken();

			if(!tkz.HasMoreTokens()) continue; // Malformed line
			wxString value = tkz.GetNextToken();

			if(key.Cmp(wxT("host")) == 0) {
				host = value;
			} else if(key.Cmp(wxT("port")) == 0) {
				port = atoi(value.mb_str());
			} else if(key.Cmp(wxT("buttonOrder")) == 0) {
				buttonOrder = decodeOrderConf(value, NUM_BUTTONS);
			} else if(key.Cmp(wxT("axisOrder")) == 0) {
				axisOrder = decodeOrderConf(value, NUM_AXIS);
			} else if(key.Cmp(wxT("tweaks")) == 0) {
				string buf = base64_decode((string)value.mb_str());
				if(buf.size() == sizeof(Tweaks))
					memcpy(&tweaks, buf.c_str(), sizeof(Tweaks));
				else LOGV("Couldn't decode tweaks from config");
			}
		}
		config.Close();

		// Remove old file
		wxRemoveFile(wxString(confLocation.c_str(), wxConvUTF8) + wxT("/") + wxT(CONF_FILE));
		wxRmdir(confLocation);

		// Now, save to new system
		savePreferences();
	} else { // Old prefs not found. Neither exist, so create new.
		savePreferences();
	}

	return true;
}

void Data::loadPreferences() {
	// Host
	host = config->Read(wxT("host"), wxEmptyString);
	// Port
	port = config->Read(wxT("host"), 3141);
	// buttonOrder
	wxString buttonOrderText, axisOrderText, tweaksText;
	if(config->Read(wxT("buttonOrder"), &buttonOrderText))
		buttonOrder = decodeOrderConf(buttonOrderText, NUM_BUTTONS);
	// axisOrder
	if(config->Read(wxT("axisOrder"), &axisOrderText))
		axisOrder = decodeOrderConf(axisOrderText, NUM_AXIS);
	// tweaks
	if(config->Read(wxT("tweaks"), &tweaksText)) {
		string buf = base64_decode((string)tweaksText.mb_str());
		if(buf.size() == sizeof(Tweaks))
			memcpy(&tweaks, buf.c_str(), sizeof(Tweaks));
		else LOGV("Couldn't decode tweaks from config");
	}

	// secureSupported
	config->Read(wxT("secureSupported"), &secureSupported, false);

	// blackKey & whiteKey
	wxString black, white;
	config->Read(wxT("blackKey"), &black, wxT("b"));
	config->Read(wxT("whiteKey"), &white, wxT("w"));
	if(black.size() > 0) blackKey = black.GetChar(0);
	if(white.size() > 0) whiteKey = white.GetChar(0);

	// computerName
	config->Read(wxT("computerName"), &computerName);
	computerName = computerName.Mid(0, 40);

	// computerUuid
	wxString tmpUuid;
	config->Read(wxT("computerUuid"), &tmpUuid);
	stringstream uuidStream((string)tmpUuid.mb_str());
	uuidStream >> computerUuid;

	// credentials
	config->SetPath(wxT("/credentials"));

	long numCredentials = 0;
	config->Read(wxT("number"), &numCredentials, 0);
	for(int i = 0; i < numCredentials; i++) {
		wxString deviceId, deviceName, psk64;
		if(
				config->Read(
					wxString::Format(wxT("%ddeviceid"), i),
					&deviceId, wxT("")) &&
				config->Read(
					wxString::Format(wxT("%ddevicename"), i),
					&deviceName, wxT("")) &&
				config->Read(
					wxString::Format(wxT("%ddevicepsk"), i),
					&psk64, wxT(""))) {
			// Successfully read
			stringstream idStream((string)deviceId.mb_str());
			boost::uuids::uuid uuid;
			idStream >> uuid;
			Credentials cred(uuid, deviceName, psk64);
			CredentialStore::credentials.push_back(cred);
			cout << "Read " << cred.deviceId << ", " << cred.deviceName.mb_str() << ", " << cred.psk64_std() << endl;
		}
	}

	config->SetPath(wxT("/"));
}

void Data::savePreferences() {
	config->Write(wxT("initialised"), true);
	config->Write(wxT("host"), host);
	config->Write(wxT("port"), port);
	config->Write(wxT("buttonOrder"), encodeOrderConf(buttonOrder, NUM_BUTTONS));
	config->Write(wxT("axisOrder"), encodeOrderConf(axisOrder, NUM_AXIS));
	config->Write(wxT("computerName"), computerName);
	config->Write(wxT("computerUuid"),
			wxString(computerUuidString().c_str(), wxConvUTF8));
	config->Write(wxT("secureSupported"), secureSupported);

	config->Write(wxT("blackKey"), (wxString)blackKey);
	config->Write(wxT("whiteKey"), (wxString)whiteKey);

	// Currently serialising tweaks the very non-portable way. Should probably change this
	char *buf = new char[sizeof(Tweaks)];
	memcpy(buf, &tweaks, sizeof(Tweaks));
	string tweakString = base64_encode((unsigned char* const)buf, sizeof(Tweaks));
	delete[] buf;
	config->Write(wxT("tweaks"), STD_TO_WX_STRING(tweakString));

	// Write credentials
	config->SetPath(wxT("/credentials"));
	config->Write(wxT("number"), (long)CredentialStore::size());
	int i = 0;
	for(vector<Credentials>::iterator it = CredentialStore::credentials.begin();
			it != CredentialStore::credentials.end(); ++it) {
		config->Write(
				wxString::Format(wxT("%ddeviceid"), i),
				wxString(boost::uuids::to_string(it->deviceId).c_str(), wxConvUTF8));
		config->Write(
				wxString::Format(wxT("%ddevicename"), i),
				it->deviceName);
		config->Write(
				wxString::Format(wxT("%ddevicepsk"), i),
				it->psk64());
		i++;
	}
	
	config->SetPath(wxT("/"));

	config->Flush();
}

wxString Data::getFilePath(wxString file)
{
	return datadir + wxFileName::GetPathSeparator() + file;
}

vector<int> Data::decodeOrderConf(wxString input, int count) {
	vector<int> ret;

	wxStringTokenizer tkz(input, wxT(","));
	for(int i = 0; i < count; i++) {
		if(tkz.HasMoreTokens()) {
			long num;
			if(!tkz.GetNextToken().ToLong(&num)) { // If fail
				num = i;
			}
			ret.push_back(num);
		} else {
			ret.push_back(i);
		}
	}
	return ret;
}

wxString Data::encodeOrderConf(vector<int> input, int count) {
	wxString ret;
	for(int i = 0; i < count; i++) {
		if(i < input.size()) {
			ret += wxString::Format(wxT("%d,"), input[i]);
		} else { // Write default out
			ret += wxString::Format(wxT("%d,"), i);
		}
	}
	return ret;
}

Tweaks Data::createDefaultTweaks() {
	Tweaks ret;
	memset(&ret, 0, sizeof(Tweaks));
	for(int i = 0; i < 2; i++) {
		ret.tilt[i].totalAngle = 120;
		ret.tilt[i].gamma = 0;
	}

	ret.rotation[0].totalAngle = 90;
	
	for(int i = 0; i < NUM_AXIS; i++) {
		ret.onScreen[i].gamma = 0;
	}
	return ret;
}

Credentials CredentialStore::createNewSet() {
	boost::uuids::uuid id = uuidGen();
	wxString name = wxT("New device");
	
	// Generate new PSK
	boost::random::uniform_int_distribution<> dist(0, 0xFF);
	string psk;
	for(int i = 0; i < PSK_LEN; i++) {
		psk += (char) dist(gen);
	}
	Credentials cred(id, name, psk);
	credentials.push_back(cred);
	Data::savePreferences();
	cout << "Creating " << cred.deviceId << ", " << cred.deviceName.mb_str() << ", " << cred.psk64_std() << endl;
	return cred;
}
