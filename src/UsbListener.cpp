#include "UsbListener.h"
#include <windows.h>
#include <winuser.h>
#include <Dbt.h>
#include <stdexcept>
#include <sstream>
#include <regex>
#include <initguid.h>
#include <Usbiodef.h>
#include <SetupAPI.h>
#include <iostream>

#pragma comment (lib, "Setupapi.lib")

std::shared_ptr<UsbListener> UsbListener::instance = nullptr;
std::mutex UsbListener::instanceMutex;

std::string getFriendlyName(wchar_t* name)
{
	HDEVINFO deviceList = SetupDiCreateDeviceInfoList(NULL, NULL);
	SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
	SetupDiOpenDeviceInterfaceW(deviceList, name, NULL, &deviceInterfaceData);
	SP_DEVINFO_DATA deviceInfo;
	ZeroMemory(&deviceInfo, sizeof(SP_DEVINFO_DATA));
	deviceInfo.cbSize = sizeof(SP_DEVINFO_DATA);
	SetupDiEnumDeviceInfo(deviceList, 0, &deviceInfo);

	DWORD size = 0;
	SetupDiGetDeviceRegistryPropertyA(deviceList, &deviceInfo, SPDRP_DEVICEDESC, NULL, NULL, NULL, &size);
	BYTE* buffer = new BYTE[size];
	SetupDiGetDeviceRegistryPropertyA(deviceList, &deviceInfo, SPDRP_DEVICEDESC, NULL, buffer, size, NULL);
	std::string deviceDesc = (char*)buffer;
	delete[] buffer;

	return deviceDesc;
}

UsbListener::UsbListener()
{
	init = false;

	// Print out current USB devices attached
	{
		HDEVINFO devicesHandle = SetupDiGetClassDevsA(&GUID_DEVINTERFACE_USB_DEVICE, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
		SP_DEVINFO_DATA deviceInfo;
		ZeroMemory(&deviceInfo, sizeof(SP_DEVINFO_DATA));
		deviceInfo.cbSize = sizeof(SP_DEVINFO_DATA);
		DWORD deviceNumber = 0;
		SP_DEVICE_INTERFACE_DATA devinterfaceData;
		devinterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

		while (SetupDiEnumDeviceInterfaces(devicesHandle, NULL, &GUID_DEVINTERFACE_USB_DEVICE, deviceNumber++, &devinterfaceData))
		{
			DWORD bufSize = 0;
			SetupDiGetDeviceInterfaceDetailW(devicesHandle, &devinterfaceData, NULL, NULL, &bufSize, NULL);
			BYTE* buffer = new BYTE[bufSize];
			PSP_DEVICE_INTERFACE_DETAIL_DATA_W devinterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)buffer;
			devinterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
			SetupDiGetDeviceInterfaceDetailW(devicesHandle, &devinterfaceData, devinterfaceDetailData, bufSize, NULL, NULL);

			wchar_t* name = devinterfaceDetailData->DevicePath;
			std::cout << "device: " << getFriendlyName(name) << std::endl;
		}
	}
}

UsbListener::~UsbListener()
{
	Stop();
}

void UsbListener::Stop()
{
	if (init)
	{
		DestroyWindow((HWND)hwnd);
	}
}

static const GUID UsbGuid =
{
	// GUID_DEVINTERFACE_USB_DEVICE
	0xA5DCBF10, 0x6530, 0x11D2, { 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED },
};

const std::regex usbNamePatternReg("\\\\?\\USB#VID_(\\w+)&PID_(\\w+)");
bool GetVidPidByDeviceName(const std::string & inStr, uint16_t & vid, uint16_t & pid)
{
	std::smatch res;
	auto isSuccess = std::regex_search(inStr, res, usbNamePatternReg, std::regex_constants::match_not_null);
	if (isSuccess)
	{
		vid = std::stoi(res.str(1), 0, 16);
		pid = std::stoi(res.str(2), 0, 16);
	}
	return isSuccess;
}
bool DeviceIsValid(PDEV_BROADCAST_DEVICEINTERFACE_A lpdbv)
{
	if (lpdbv == nullptr)
	{
		return false;
	}

	// Check if it's a USB device.
	bool isUsb = (memcmp(&(lpdbv->dbcc_classguid), &UsbGuid, sizeof(GUID)) == 0);
	return isUsb;
}

int64_t UsbListener::HandleHotplugMessage(void *hwnd, uint32_t uint, uint64_t wparam, int64_t lparam)
{
	switch (uint)
	{
	case WM_NCCREATE: // before window creation
		return 1;
		break;

	case WM_CREATE:
	{
		LPCREATESTRUCT params = (LPCREATESTRUCT)lparam;
		DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;
		ZeroMemory(&NotificationFilter, sizeof(NotificationFilter));
		NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
		NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
		HDEVNOTIFY dev_notify = RegisterDeviceNotification(hwnd, &NotificationFilter,
			DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);
		if (dev_notify == NULL)
		{
			throw std::runtime_error("Could not register for device notifications!");
		}
		break;
	}

	case WM_ACTIVATEAPP:
	{
		break;
	}
	case WM_DEVICECHANGE:
	{
		if (wparam != DBT_DEVICEARRIVAL && wparam != DBT_DEVICEREMOVECOMPLETE)
		{
			break;
		}
		PDEV_BROADCAST_HDR lpdb = (PDEV_BROADCAST_HDR)lparam;
		PDEV_BROADCAST_DEVICEINTERFACE_A lpdbv = (PDEV_BROADCAST_DEVICEINTERFACE_A)lpdb;
		if (lpdb == nullptr)
		{
			break;
		}
		if (DeviceIsValid(lpdbv) == false)
		{
			break;
		}

		if (lpdb->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
		{
			if (usbDeviceChangeCallback != nullptr)
			{
				usbDeviceChangeCallback(std::string(lpdbv->dbcc_name), wparam == DBT_DEVICEARRIVAL);
			}
		}
		break;
	}

	default:
		break;

	}
	return 0L;
}

LRESULT CALLBACK _MessageHandler(HWND hwnd, UINT uint, WPARAM wparam, LPARAM lparam)
{
	return UsbListener::GetInstance()->HandleHotplugMessage(hwnd, uint, wparam, lparam);
}

void UsbListener::SetDeviceChangeCallback(UsbDeviceChangeCallback callback)
{
	this->usbDeviceChangeCallback = callback;
}
bool UsbListener::Start()
{
	if (init)
	{
		return true;
	}
	void *windowHandle = GetModuleHandle(0);

	const char CLASS_NAME[] = "HOTPLUG LISTENER";
	HINSTANCE hInstance = (HINSTANCE)windowHandle;

	WNDCLASSA wc = {};

	wc.lpfnWndProc = _MessageHandler;
	wc.hInstance = hInstance;
	wc.lpszClassName = CLASS_NAME;

	RegisterClassA(&wc);
	this->hwnd = CreateWindowExA(
		0,                              // Optional window styles.
		CLASS_NAME,                     // Window class
		"",    // Window text
		WS_POPUPWINDOW,            // Window style
								   // Size and position
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL,       // Parent window    
		NULL,       // Menu
		hInstance,  // Instance handle
		NULL        // Additional application data
	);
	if (hwnd == nullptr)
	{
		auto errorCode = GetLastError();
		return false;
	}

	init = true;
	return true;
}

void UsbListener::PeekHotplugMessage()
{
	MSG msg;
	PeekMessage(&msg, NULL, 0, 0, 0);
}

std::shared_ptr<UsbListener> UsbListener::GetInstance()
{
	std::lock_guard<std::mutex> lock(instanceMutex);
	if (instance == nullptr)
	{
		instance = std::shared_ptr<UsbListener>(new UsbListener());
	}
	return instance;
}