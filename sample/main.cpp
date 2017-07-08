#pragma once
#include <memory>
#include <functional>
#include <iostream>
#include <string>

#include "UsbListener.h"

void Callback(const std::string &deviceName, bool plugOn)
{
	std::cout << "device plug " << (plugOn ? "on" : "off") << std::endl;
	std::cout << "name: " << deviceName << std::endl;
	std::cout << std::endl;
}

int main()
{
	bool threadStop = false;
	auto thread = std::thread([&threadStop]()
	{
		auto listener = UsbListener::GetInstance();
		listener->SetDeviceChangeCallback(Callback);
		listener->Start();
		while (!threadStop)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			listener->PeekHotplugMessage();
		}
		listener->Stop();
	});

	std::string stop;
	std::cin >> stop;
	threadStop = true;
	thread.join();
	return 0;
}