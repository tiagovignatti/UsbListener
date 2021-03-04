#include "UsbListener.h"
#include <windows.h>
#include <winuser.h>
#include <Dbt.h>
#include <stdexcept>
#include <sstream>
#include <regex>

std::shared_ptr<UsbListener> UsbListener::instance = nullptr;
std::mutex UsbListener::instanceMutex;

UsbListener::UsbListener()
{
	init = false;
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