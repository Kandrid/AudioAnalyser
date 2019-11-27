#include <iostream>
#include <Windows.h>
#include <mutex>
#include "SFML/Audio.hpp"
#include "SFML/Graphics.hpp"
#include "fft.h"
#include "complex.h"

std::mutex mutex;
std::vector<double> frequencies;

class Recorder : public sf::SoundRecorder
{
	virtual bool onStart() // optional
	{
		// initialize whatever has to be done before the capture starts
		/*
		HWND console = GetConsoleWindow();
		RECT r;
		GetWindowRect(console, &r); //stores the console's current dimensions
		MoveWindow(console, r.left, r.top, 300, 520, TRUE);*/
		std::cout << "Recorder Started" << std::endl;
		setProcessingInterval(sf::Time(sf::milliseconds(5)));
		// return true to start the capture, or false to cancel it
		return true;
	}

	double* truncate(complex* const data, size_t size, size_t newSize) {
		double* result = new double[newSize];
		double buffer;
		size_t chunkSize = size / newSize / 23;
		size_t chunk = 0;

		for (size_t i = 0; i < size && chunk < newSize; i += chunkSize) {
			buffer = 0;
			for (size_t j = 0; j < chunkSize; j++) {
				if (data[i + j].norm() > 0) {
					buffer += data[i + j].norm();
				}
			}
			result[chunk++] = buffer / chunkSize;
		}

		return result;
	}

	void clear() {
		COORD topLeft = { 0, 0 };
		HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
		CONSOLE_SCREEN_BUFFER_INFO screen;
		DWORD written;

		GetConsoleScreenBufferInfo(console, &screen);
		FillConsoleOutputCharacterA(
			console, ' ', screen.dwSize.X * screen.dwSize.Y, topLeft, &written
		);
		SetConsoleCursorPosition(console, topLeft);
	}

	virtual bool onProcessSamples(const sf::Int16* samples, size_t sampleCount)
	{
		// do something useful with the new chunk of samples
		const size_t size = 4096;
		complex* const complexSamples = new complex[size];
		
		for (int i = 0; i < size; i++) {
			if (i < sampleCount) {
				complexSamples[i] = samples[i];
			}
			else {
				complexSamples[i] = 0;
			}
		}

		if (!CFFT::Forward(complexSamples, size)) {
			std::cout << "Error: FFT execution failed" << std::endl;
			return false;
		}

		const uint16_t bars = 30;
		double* transform = truncate(complexSamples, size, bars);
		const double scale1 = 160;
		const double scale2 = 1.1e-8;
		const double sensitivity = 0;

		mutex.lock();
		//frequencies.clear();
		if (frequencies.size() == 0) {
			for (int i = 0; i < bars; i++) {
				double magnitude = log10(transform[i] * scale2) * scale1;
				frequencies.push_back(magnitude);
			}
		}
		else {
			for (int i = 0; i < bars; i++) {
				double magnitude = log10(transform[i] * scale2) * scale1;
				frequencies[i] = (frequencies[i] * 0.333 + magnitude * 0.666);
			}
		}
		mutex.unlock();

		//std::cout << sampleCount << std::endl;

		delete[] transform;
		delete[] complexSamples;

		// return true to continue the capture, or false to stop it
		return true;
	}

	virtual void onStop() // optional
	{
		// clean up whatever has to be done after the capture is finished
		std::cout << "Recorder Stopped" << std::endl;
	}
};

void renderingThread(sf::RenderWindow* window)
{
	// activate the window's context
	window->setActive(true);

	// the rendering loop
	while (window->isOpen())
	{
		// clear the window with black color
		window->clear(sf::Color::Black);
		mutex.lock();
		// draw everything here...
		for (int i = 0; i < frequencies.size(); i++) {
			double magnitude = frequencies[i];
			if (magnitude < 3) {
				magnitude = 3;
			}
			else if (magnitude > 500) {
				magnitude = 500;
			}
			double barWidth = 1280 / frequencies.size() * 0.95;
			double margin_x = (1280 - barWidth * frequencies.size()) / 2;
			double margin_y = 50;
			sf::RectangleShape rectangle(sf::Vector2f(barWidth * 0.9, -magnitude));
			rectangle.setPosition(i * barWidth + margin_x, 600 - margin_y);
			rectangle.setFillColor(sf::Color(0, 0, 255));
			window->draw(rectangle);
		}
		mutex.unlock();
		// end the current frame
		window->display();
	}
}

int main() {
	frequencies = std::vector<double>();
	// create the window (remember: it's safer to create it in the main thread due to OS limitations)
	sf::RenderWindow window(sf::VideoMode(1280, 600), "OpenGL");
	window.setVerticalSyncEnabled(true);
	// deactivate its OpenGL context
	window.setActive(false);

	// launch the rendering thread
	sf::Thread thread(&renderingThread, &window);
	thread.launch();

	// get the available sound input device names
	std::vector<std::string> availableDevices = sf::SoundRecorder::getAvailableDevices();

	// choose a device
	std::string inputDevice = availableDevices[0];

	if (!Recorder::isAvailable())
	{
		// error...
		std::cout << "Error: Recorder not available" << std::endl;
		return -1;
	}

	Recorder recorder;

	if (!recorder.setDevice(inputDevice))
	{
		std::cout << "Error: Recorder selection failed" << std::endl;
		return -1;
	}

	recorder.start();

	// the event/logic/whatever loop
	while (window.isOpen())
	{
		// check all the window's events that were triggered since the last iteration of the loop
		sf::Event event;
		while (window.pollEvent(event))
		{
			// "close requested" event: we close the window
			if (event.type == sf::Event::Closed) {
				window.close();
			}
		}
	}

	recorder.stop();

	return 0;
}