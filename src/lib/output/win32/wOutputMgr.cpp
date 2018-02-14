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
#include "wOutputMgr.hpp"

#include <sstream>

#include "types.hpp"
#include "data.hpp"

#include "net/connection.hpp"
#include "log.hpp"
#include "winOutputs.hpp"

#include <windows.h>

using namespace std;
using namespace droidpad;
using namespace droidpad::decode;
using namespace droidpad::win32;

OutputManager::OutputManager(const int type, const int numAxes, const int numButtons) :
	IOutputManager(type, numAxes, numButtons)
{
	switch(type) {
		case MODE_JS:
			joystick = new JSOutputs;
			break;
		default:
			joystick = NULL;
			break;
	}

	if(joystick) {
		int result = joystick->OpenJoystick();
		if(result) { // If fails
			stringstream err;
			int ret = -1;
			switch(result) {
				case ERROR_FILE_NOT_FOUND:
					err << "vJoy device not found / not installed.";
					ret = ERROR_NO_VJOY;
					break;
				default:
					err << "Couldn't open vJoy handle, error is " << result << ".";
					break;
			}
			throw OutputException(ret, err.str());
		}
	}
}

OutputManager::~OutputManager() {
	if(joystick) delete joystick;
}

void OutputManager::SendJSData(const DPJSData& data, bool firstIteration) {
	INPUT_DATA pos;
	// data.axes contains signed integer from -AXIS_SIZE to AXIS_SIZE
	// map it to range from 0 to JS_OFFSET*2+1. Zero is mapped to JS_OFFSET
	pos.axisX  = data.axes.size() >= 1 ? ((float)data.axes[0]/AXIS_SIZE)*(JS_OFFSET+1) + JS_OFFSET : JS_OFFSET;
	pos.axisY  = data.axes.size() >= 2 ? ((float)data.axes[1]/AXIS_SIZE)*(JS_OFFSET+1) + JS_OFFSET : JS_OFFSET;
	pos.axisZ  = data.axes.size() >= 3 ? ((float)data.axes[2]/AXIS_SIZE)*(JS_OFFSET+1) + JS_OFFSET : JS_OFFSET;
	pos.axisRX = data.axes.size() >= 4 ? ((float)data.axes[3]/AXIS_SIZE)*(JS_OFFSET+1) + JS_OFFSET : JS_OFFSET;
	pos.axisRY = data.axes.size() >= 5 ? ((float)data.axes[4]/AXIS_SIZE)*(JS_OFFSET+1) + JS_OFFSET : JS_OFFSET;
	pos.axisRZ = data.axes.size() >= 6 ? ((float)data.axes[5]/AXIS_SIZE)*(JS_OFFSET+1) + JS_OFFSET : JS_OFFSET;
	pos.slider = data.axes.size() >= 7 ? ((float)data.axes[6]/AXIS_SIZE)*(JS_OFFSET+1) + JS_OFFSET : JS_OFFSET;
	pos.dial   = data.axes.size() >= 8 ? ((float)data.axes[7]/AXIS_SIZE)*(JS_OFFSET+1) + JS_OFFSET : JS_OFFSET;
	pos.buttons = 0;
	for(int i = 0; i < data.buttons.size(); i++) {
		pos.buttons += data.buttons[i] ? 0x1 << i : 0;
	}
	joystick->SendPositions(pos);
}

void OutputManager::SendMouseData(const DPMouseData& data, bool firstIteration)
{
	if(!WinOutputs::SendMouseEvent(data.x / 400, -data.y / 400, data.bLeft, data.bMiddle, data.bRight, firstIteration ? data.incrementalScrollDelta : 0)) // TODO: Customise scale factors?
	{
		LOGWwx(wxT("SendInput failed") + GetLastError());
	}
}

void OutputManager::SendSlideData(const DPSlideData& data, bool firstIteration)
{
	LOGVwx(wxString::Format(wxT("Keycode: %c"), Data::blackKey));
	WinOutputs::SendKeystroke(VK_UP,	data.prev);
	WinOutputs::SendKeystroke(VK_DOWN,	data.next);
	WinOutputs::SendKeystroke(VK_F5,	data.start);
	WinOutputs::SendKeystroke(VK_ESCAPE,	data.finish);
	WinOutputs::SendKeystroke(getKeycode(Data::whiteKey),data.white);
	WinOutputs::SendKeystroke(getKeycode(Data::blackKey),data.black);
	WinOutputs::SendKeystroke(VK_HOME,	data.beginning);
	WinOutputs::SendKeystroke(VK_END,	data.end);
}

#define WIN_ABS_AXIS_SIZE 65536

void OutputManager::SendTouchData(const decode::DPTouchData& data, bool firstIteration) {
	int newx = (data.x + AXIS_SIZE) * WIN_ABS_AXIS_SIZE / AXIS_SIZE / 2;
	int newy = (-data.y + AXIS_SIZE) * WIN_ABS_AXIS_SIZE / AXIS_SIZE / 2;
//	wxString dbg;
//	dbg.Printf(wxT("Position: %d, %d"), newx, newy);
//	LOGVwx(dbg);
	if(!WinOutputs::SendAbsMouseEvent(newx, newy, data.bLeft, data.bMiddle, data.bRight, firstIteration ? data.incrementalScrollDelta : 0)) // TODO: Customise scale factors?
	{
		LOGWwx(wxT("SendInput failed: ") + GetLastError());
	}
}

int OutputManager::getKeycode(char letter) {
	// Windows a-z keycodes are just A-Z ASCII codes
	if(isalpha(letter)) return toupper(letter);
	if(isdigit(letter)) return letter;
	return 'B'; // Fall through
}
