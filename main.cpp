﻿#include <iostream>
#include <Windows.h>
#include <mutex>
#include "SFML/Audio.hpp"
#include "SFML/Graphics.hpp"
#include "fft.h"
#include "complex.h"

std::mutex mutex;
std::vector<double> frequencies;
const uint32_t WIDTH = 1280;
const uint32_t HEIGHT = 600;
const uint8_t BARS = 30;
double scale1 = 160;
double scale2 = 1.1e-8;
double sensitivity = 0;

class Recorder : public sf::SoundRecorder
{
	virtual bool onStart()
	{
		// initialize whatever has to be done before the capture starts
		std::cout << "Recorder Started" << std::endl;
		setProcessingInterval(sf::Time(sf::milliseconds(5)));
		// return true to start the capture, or false to cancel it
		return true;
	}

	double* truncate(complex* const data, size_t size, size_t newSize) {
		// truncate the data array by averaging the values over chunks
		double* result = new double[newSize];
		double buffer;
		const size_t chunkSize = size / newSize / 23;
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

		double* transform = truncate(complexSamples, size, BARS);

		// protect access to variables of external threads
		mutex.lock();

		if (frequencies.size() == 0) {
			for (int i = 0; i < BARS; i++) {
				double magnitude = log10(transform[i] * scale2) * scale1;
				frequencies.push_back(magnitude);
			}
		}
		else {
			for (int i = 0; i < BARS; i++) {
				double magnitude = log10(transform[i] * scale2) * scale1;
				frequencies[i] = (frequencies[i] * 0.333 + magnitude * 0.666);
			}
		}

		mutex.unlock();

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

	std::cout << "[Right/Left] Increase/Decrease Logarithmic Scale" << std::endl;
	std::cout << "[Up/Down] Increase/Decrease Linear Scale" << std::endl;

	// the rendering loop
	while (window->isOpen())
	{
		// clear the window with black color
		window->clear(sf::Color::Black);

		// protect access to variables of external threads
		mutex.lock();

		// draw everything here...
		for (int i = 0; i < frequencies.size(); i++) {
			double magnitude = frequencies[i];
			if (magnitude < 3) {
				magnitude = 3;
			}
			else if (magnitude > HEIGHT * 0.86) {
				magnitude = HEIGHT * 0.86;
			}
			double barWidth = WIDTH / frequencies.size() * 0.95;
			double margin_x = (WIDTH - barWidth * frequencies.size()) / 2;
			double margin_y = HEIGHT * 0.07;
			sf::RectangleShape rectangle(sf::Vector2f(barWidth * 0.9, -magnitude));
			rectangle.setPosition(i * barWidth + margin_x, HEIGHT - margin_y);
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
	sf::RenderWindow window(sf::VideoMode(WIDTH, HEIGHT), "OpenGL");
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
			else if (event.type == sf::Event::KeyPressed) {
				// protect access to variables of external threads
				mutex.lock();

				switch (event.key.code) {
				case sf::Keyboard::Up:
					if (scale1 < 1000) {
						scale1 *= 1.1;
						std::cout << "[+] Linear Scale: " << scale1 << std::endl;
					}
					break;
				case sf::Keyboard::Down:
					if (scale1 > 10) {
						scale1 /= 1.1;
						std::cout << "[-] Linear Scale: " << scale1 << std::endl;
					}
					break;
				case sf::Keyboard::Right:
					if (scale2 < 1e-3) {
						scale2 *= 1.1;
						std::cout << "[-] Logarithmic Scale: " << scale2 << std::endl;
					}
					break;
				case sf::Keyboard::Left:
					if (scale2 > 1e-12) {
						scale2 /= 1.1;
						std::cout << "[-] Logarithmic Scale: " << scale2 << std::endl;
					}
					break;
				}

				mutex.unlock();
			}
		}
	}

	recorder.stop();

	return 0;
}