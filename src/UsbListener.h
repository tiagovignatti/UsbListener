#pragma once
#include <functional>
#include <memory>
#include <mutex>

class UsbListener
{
public:
	typedef std::function<void(const std::string &deviceName, bool plugOn)> UsbDeviceChangeCallback;
private:
	static std::shared_ptr<UsbListener> instance;
	static std::mutex instanceMutex;
protected:
	bool init;
	void *hwnd;
	UsbDeviceChangeCallback usbDeviceChangeCallback;

	UsbListener();
	UsbListener(const UsbListener&) = delete;
public:
	static std::shared_ptr<UsbListener> GetInstance();
	virtual ~UsbListener();
	void SetDeviceChangeCallback(UsbDeviceChangeCallback callback);
	virtual bool Start();
	virtual void Stop();
	void PeekHotplugMessage();
	int64_t HandleHotplugMessage(void *hwnd, uint32_t uint, uint64_t wparam, int64_t lparam);
	
};