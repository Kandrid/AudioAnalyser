#include <iostream>
#include <Windows.h>
#include <mutex>
#include "SFML/Audio.hpp"
#include "SFML/Graphics.hpp"
#include "fft.h"
#include "complex.h"

std::mutex mutex;
std::vector<double> frequencies;
const uint32_t WIDTH = 1280;
const uint32_t HEIGHT = 365;
uint16_t bars = 40;
double scale1 = 160;
double scale2 = 1.1e-8;
double smoothing = 0.4;
double averageMax = HEIGHT / 2;
uint32_t autoScaleCycles = 300;
uint16_t autoScaleCount = 0;
bool autoScale = true;
double colourCounter = 0;
sf::Color gradient[256 * 6];
double colourChange = 0.01;
double shadingRatio = 0.8;

class Recorder : public sf::SoundRecorder
{
	virtual bool onStart()
	{
		// initialize whatever has to be done before the capture starts
		std::cout << "Recorder Started" << std::endl;
		setProcessingInterval(sf::Time(sf::milliseconds(10)));
		// return true to start the capture, or false to cancel it
		return true;
	}

	double* truncate(complex* const data, size_t size, size_t newSize) {
		// truncate the data array by averaging the values over chunks
		double* result = new double[newSize];
		double buffer;
		const size_t chunkSize = size / newSize / 18;
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
			complexSamples[i] = i < sampleCount ? samples[i] : 0;
		}

		if (!CFFT::Forward(complexSamples, size)) {
			std::cout << "Error: FFT execution failed" << std::endl;
			return false;
		}

		double* transform = truncate(complexSamples, size, bars);

		// protect access to variables of external threads
		mutex.lock();

		if (frequencies.size() == 0) {
			for (int i = 0; i < bars; i++) {
				double magnitude = log10(transform[i] * scale2) * scale1;
				frequencies.push_back(magnitude);
			}
		}
		else {
			for (int i = 0; i < bars; i++) {
				double magnitude = log10(transform[i] * scale2) * scale1;
				frequencies[i] = (frequencies[i] * smoothing + magnitude * (1 - smoothing));
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

	std::cout << "--------------------------------------------------" << std::endl;

	std::cout << "[Controls]" << std::endl;

	std::cout << "[Right/Left] Increase/Decrease Logarithmic Scaling" << std::endl;
	std::cout << "[Up/Down] Increase/Decrease Linear Scaling" << std::endl;
	std::cout << "[Space] Enable/Disable Auto Scaling" << std::endl;
	std::cout << "[CTRL + Up/Down] Increase/Decrease Bars" << std::endl;
	std::cout << "[CTRL + Right/Left] Increase/Decrease Smoothing" << std::endl;
	std::cout << "[Alt + Up/Down] Increase/Decrease Hue shift Speed" << std::endl;
	std::cout << "[Alt + Right/Left] Increase/Decrease Shading" << std::endl;

	std::cout << "--------------------------------------------------" << std::endl;

	std::cout << "[Defaults]" << std::endl;

	std::cout << "Logarithmic Scaling: " << scale2 << std::endl;
	std::cout << "Linear Scaling: " << scale1 << std::endl;
	std::cout << "Auto Scaling: " << autoScale << std::endl;
	std::cout << "Bars: " << bars << std::endl;
	std::cout << "Smoothing: " << smoothing << std::endl;
	std::cout << "Hue Shift Speed: " << colourChange << std::endl;
	std::cout << "Shading: " << shadingRatio << std::endl;

	std::cout << "--------------------------------------------------" << std::endl;

	std::cout << "[Logs]" << std::endl;

	// the rendering loop
	while (window->isOpen())
	{
		// clear the window with black color
		window->clear(sf::Color::Black);

		// protect access to variables of external threads
		mutex.lock();

		// draw everything here...
		double max = 1;
		for (int i = 0; i < frequencies.size(); i++) {
			double magnitude = frequencies[i];
			if (magnitude > max) {
				max = magnitude;
			}
			if (magnitude < 3) {
				magnitude = 3;
			}
			else if (magnitude > HEIGHT * 0.86) {
				magnitude = HEIGHT * 0.86;
			}
			double margin_x = WIDTH * 0.035;
			double margin_y = HEIGHT * 0.07;
			double barWidth = (WIDTH - 2 * margin_x) / frequencies.size(); 
			sf::RectangleShape rectangle(sf::Vector2f(barWidth * 0.9, -magnitude));
			rectangle.setPosition(i * barWidth + margin_x + (0.1 * barWidth / frequencies.size() / 2), HEIGHT - margin_y);
			sf::Color colour = gradient[(int)floor(colourCounter)];
			double shader = shadingRatio * (1 - magnitude / (HEIGHT * 0.86));
			if (shader > 1.0) { shader = 1; }
			colour.r -= colour.r * shader;
			colour.g -= colour.g * shader;
			colour.b -= colour.b * shader;
			rectangle.setFillColor(colour);
			if (colourCounter >= 256.0 * 6.0)
			{ 
				colourCounter = 0; 
			}
			else {
				colourCounter += colourChange;
			}
			window->draw(rectangle);
		}

		if (autoScale && (max > 1 || max > HEIGHT * 0.85)) {
			double ratio = 1.0 / autoScaleCycles;
			averageMax = max * ratio + averageMax * (1 - ratio);
			autoScaleCount++;
			if (autoScaleCount > 100) {
				autoScaleCount = 0;
				if (averageMax < HEIGHT * 0.5) {
					averageMax = HEIGHT / 2;
					if (scale2 < 1e-3) {
						scale2 *= 1.1;
						std::cout << "[+] Logarithmic Scale: " << scale2 << std::endl;
					}
				}
				else if (averageMax > HEIGHT * 0.7 || max > HEIGHT * 0.85) {
					averageMax = HEIGHT / 2;
					if (scale2 > 1e-12) {
						scale2 /= 1.1;
						std::cout << "[-] Logarithmic Scale: " << scale2 << std::endl;
					}
				}
			}
		}

		mutex.unlock();

		// end the current frame
		window->display();
	}
}

int main() {
	HWND console = GetConsoleWindow();
	RECT r;
	GetWindowRect(console, &r); //stores the console's current dimensions
	MoveWindow(console, r.left, r.top, 460, 450, TRUE);

	for (int i = 0; i < 256; i++) {
		gradient[i] = sf::Color(255, i, 0);
	}
	for (int i = 0; i < 256; i++) {
		gradient[i + 256 * 1] = sf::Color(255 - i, 255, 0);
	}
	for (int i = 0; i < 256; i++) {
		gradient[i + 256 * 2] = sf::Color(0, 255, i);
	}
	for (int i = 0; i < 256; i++) {
		gradient[i + 256 * 3] = sf::Color(0, 255 - i, 255);
	}
	for (int i = 0; i < 256; i++) {
		gradient[i + 256 * 4] = sf::Color(i, 0, 255);
	}
	for (int i = 0; i < 256; i++) {
		gradient[i + 256 * 5] = sf::Color(255, 0, 255 - i);
	}

	frequencies = std::vector<double>();

	// create the window (remember: it's safer to create it in the main thread due to OS limitations)
	sf::RenderWindow window(sf::VideoMode(WIDTH, HEIGHT), "OpenGL");
	window.setVerticalSyncEnabled(true);
	window.setTitle("Audio Visualizer");
	sf::Image icon;
	icon.loadFromFile("icon.png");
	window.setIcon(icon.getSize().x, icon.getSize().y, icon.getPixelsPtr());
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
				if (sf::Keyboard::isKeyPressed(sf::Keyboard::LControl) || sf::Keyboard::isKeyPressed(sf::Keyboard::RControl)) {
					switch (event.key.code) {
					case sf::Keyboard::Up:
						if (bars < 100) {
							bars++;
							frequencies.clear();
							std::cout << "[+] Bars: " << bars << std::endl;
						}
						break;
					case sf::Keyboard::Down:
						if (bars > 1) {
							bars--;
							frequencies.clear();
							std::cout << "[-] Bars: " << bars << std::endl;
						}
						break;

					case sf::Keyboard::Right:
						if (smoothing < 0.95) {
							smoothing += 0.05;
							std::cout << "[+] Smoothing: " << smoothing << std::endl;
						}
						break;
					case sf::Keyboard::Left:
						if (smoothing >= 0.05) {
							smoothing -= 0.05;
							if (smoothing < 0.04) {
								smoothing = 0;
							}
							std::cout << "[-] Smoothing: " << smoothing << std::endl;
						}
						break;
					}
				}
				else if (sf::Keyboard::isKeyPressed(sf::Keyboard::LAlt) || sf::Keyboard::isKeyPressed(sf::Keyboard::RAlt)) {
					switch (event.key.code) {
					case sf::Keyboard::Up:
						if (colourChange < 0.5) {
							colourChange += 0.005;
							std::cout << "[+] Hue Shift Speed: " << colourChange << std::endl;
						}
						break;
					case sf::Keyboard::Down:
						if (colourChange > 0) {
							colourChange -= 0.005;
							if (colourChange < 0.004) {
								colourChange = 0;
							}
							std::cout << "[-] Hue Shift Speed: " << colourChange << std::endl;
						}
						break;

					case sf::Keyboard::Right:
						if (shadingRatio < 3) {
							shadingRatio += 0.1;
							std::cout << "[+] Shading: " << shadingRatio << std::endl;
						}
						break;
					case sf::Keyboard::Left:
						if (shadingRatio >= 0.1) {
							shadingRatio -= 0.1;
							if (shadingRatio < 0.05) {
								shadingRatio = 0;
							}
							std::cout << "[-] Shading: " << shadingRatio << std::endl;
						}
						break;
					}
				}
				else {
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
							std::cout << "[+] Logarithmic Scale: " << scale2 << std::endl;
						}
						break;
					case sf::Keyboard::Left:
						if (scale2 > 1e-12) {
							scale2 /= 1.1;
							std::cout << "[-] Logarithmic Scale: " << scale2 << std::endl;
						}
						break;
					case sf::Keyboard::Space:
						autoScale = !autoScale;
						if (autoScale) {
							std::cout << "[+] Auto Scaling: Enabled" << std::endl;
						}
						else {
							std::cout << "[-] Auto Scaling: Disabled" << std::endl;
						}
						break;
					}
				}

				mutex.unlock();
			}
		}
	}

	recorder.stop();

	return 0;
}