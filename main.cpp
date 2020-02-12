#include <iostream>
#include <fstream>
#include <Windows.h>
#include <mutex>
#include <dwmapi.h>
#include "SFML/Audio.hpp"
#include "SFML/Graphics.hpp"
#include "fft.h"
#include "complex.h"

const std::string version = "1.8";
std::mutex mutex;
std::vector<double> frequencies;
std::vector<double> peaks;
const uint32_t WIDTH = 1920;
const uint32_t HEIGHT = 1080;
const uint32_t autoScaleCycles = 300;
const uint32_t processingInterval = 10;
const uint32_t bufferSize = 16384;
uint16_t bars = 50;
uint16_t autoScaleCount = 0;
uint16_t maxFrequency = 3000;
uint16_t peakDecay = 15;
uint16_t divisions = 10;
double scale1 = 285.0;
double scale2 = 4.0e-9;
double smoothing = 0.4;
double averageMax = HEIGHT / 2;
double colourChange = 1;
double shadingRatio = 0.8;
double colourCounter = 0;
double colourOffset = 255 * 2;
double gapRatio = 0.75;
bool autoScale = false;
bool delayedPeaks = true;
bool decaySmoothing = false;
bool classic = false;
bool borderless = false;
bool inter = false;
sf::Color gradient[256 * 6];

class Recorder : public sf::SoundRecorder
{
	virtual bool onStart()
	{
		// initialize whatever has to be done before the capture starts
		std::cout << "Recorder Started" << std::endl;
		setProcessingInterval(sf::Time(sf::milliseconds(processingInterval)));
		// return true to start the capture, or false to cancel it
		return true;
	}

	double* truncate(complex* const data, size_t size, size_t newSize) {
		// truncate the data array by averaging the values over chunks
		double* result = new double[newSize];
		double buffer;
		double chunkSize = ((double)size / (double)newSize) / (20000.0 / (double)maxFrequency);
		size_t chunk = 0;

		for (double i = 0; i < size && chunk < newSize; i += chunkSize) {
			buffer = 0;
			for (size_t j = 0; j < chunkSize; j++) {
				if (data[(int)floor(i) + j].norm() > 0) {
					buffer += data[(int)floor(i) + j].norm();
				}
			}
			result[chunk++] = buffer / chunkSize;
		}

		return result;
	}

	virtual bool onProcessSamples(const sf::Int16* samples, size_t sampleCount)
	{
		// do something useful with the new chunk of samples
		complex* const complexSamples = new complex[bufferSize];
		
		for (int i = 0; i < bufferSize; i++) {
			complexSamples[i] = i < sampleCount ? samples[i] : 0;
		}

		if (!CFFT::Forward(complexSamples, bufferSize)) {
			std::cout << "Error: FFT execution failed" << std::endl;
			return false;
		}

		double* transform = truncate(complexSamples, bufferSize / 2, bars);

		// protect access to variables of external threads
		mutex.lock();

		if (frequencies.size() == 0) {
			for (int i = 0; i < bars; i++) {
				double magnitude = log10(transform[i] * scale2) * scale1;
				frequencies.push_back(magnitude);
				peaks.push_back(magnitude);
			}
		}
		else {
			for (int i = 0; i < bars; i++) {
				double magnitude = log10(transform[i] * scale2) * scale1;
				if (decaySmoothing) {
					frequencies[i] = magnitude > frequencies[i] ? magnitude : (frequencies[i] * smoothing + magnitude * (1 - smoothing));
				}
				else {
					frequencies[i] = (frequencies[i] * smoothing + magnitude * (1 - smoothing));
				}
				if (delayedPeaks) {
					if (frequencies[i] > peaks[i]) {
						peaks[i] = frequencies[i];
					}
					else if (peaks[i] > 1) {
						peaks[i] -= peakDecay;
						if (frequencies[i] > peaks[i]) {
							peaks[i] = frequencies[i];
						}
					}
				}
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

double interpolate(double pos) {
	if (frequencies.size() == 0) return 0;
	if (pos < 0 || pos >= frequencies.size()) {
		std::cout << pos;
		throw 1;
	}
	double diff_f, diff, lower, upper, acc, result;
	lower = floor(pos);
	upper = ceil(pos);
	diff = pos - lower;
	diff_f = frequencies[lower] - frequencies[upper];
	if (diff == 0 || diff_f == 0) return frequencies[pos];
	acc = 4 * diff_f;
	if (diff <= 0.5) {
		result = frequencies[lower] - acc * diff * diff / 2;
	}
	else {
		double v = diff_f - acc / 4;
		diff = upper - pos;
		result = frequencies[upper] + (v * diff + acc * diff * diff) / 2;
	}
	return result;
}

void renderingThread(sf::RenderWindow* window)
{
	// activate the window's context
	window->setActive(true);

	std::cout << "------------------------------------------------------------------------" << std::endl;

	std::cout << "[Controls]" << std::endl;

	std::cout << "[Right/Left] Increase/Decrease Logarithmic Scaling" << std::endl;
	std::cout << "[Up/Down] Increase/Decrease Linear Scaling" << std::endl;
	std::cout << "[Space] Enable/Disable Auto Scaling" << std::endl;
	std::cout << "[BackSpace] Enable/Disable Decaying Peaks" << std::endl;
	std::cout << "[Shift + BackSpace] Decay-Only Smoothing/Normal Smoothing" << std::endl;
	std::cout << "[Ctrl + Up/Down] Increase/Decrease Bars" << std::endl;
	std::cout << "[Ctrl + Right/Left] Increase/Decrease Smoothing" << std::endl;
	std::cout << "[Ctrl + Enter] Classic/Normal Display Mode" << std::endl;
	std::cout << "[Alt + Up/Down] Increase/Decrease Hue shift Speed" << std::endl;
	std::cout << "[Alt + Right/Left] Increase/Decrease Shading" << std::endl;
	std::cout << "[Alt + BackSpace] Enable/Disable Bar Interpolation" << std::endl;
	std::cout << "[Shift + Up/Down] Increase/Decrease Max Frequency" << std::endl;
	std::cout << "[Shift + Right/Left] Increase/Decrease Peak Decay Speed" << std::endl;
	std::cout << "[Ctrl + Shift + Up/Down] Increase/Decrease Intensity Based Colour Offset" << std::endl;
	std::cout << "[Ctrl + Shift + Left/Right] Increase/Decrese Bar Gap Ratio" << std::endl;
	std::cout << "[Ctrl + Shift + Alt + Up/Down] Increase/Decrease Classic Mode Divisions" << std::endl;

	std::cout << "------------------------------------------------------------------------" << std::endl;

	std::cout << "[Defaults]" << std::endl;

	std::cout << "Logarithmic Scaling: " << scale2 << std::endl;
	std::cout << "Linear Scaling: " << scale1 << std::endl;
	std::cout << "Auto Scaling: " << autoScale << std::endl;
	std::cout << "Bars: " << bars << std::endl;
	std::cout << "Smoothing: " << smoothing << std::endl;
	std::cout << "Smoothing Mode: " << decaySmoothing << std::endl;
	std::cout << "Hue Shift Speed: " << colourChange << std::endl;
	std::cout << "Shading: " << shadingRatio << std::endl;
	std::cout << "Max Frequency: " << maxFrequency << std::endl;
	std::cout << "Decaying Peaks: " << delayedPeaks << std::endl;
	std::cout << "Peak Decay Speed: " << peakDecay << std::endl;
	std::cout << "Colour Intensity Offset: " << colourOffset << std::endl;
	std::cout << "Bar Gap Ratio: " << gapRatio << std::endl;
	std::cout << "Display Mode: " << classic << std::endl;
	std::cout << "Classic Mode Divisions: " << divisions << std::endl;
	std::cout << "Bar Interpolation: " << inter << std::endl;

	std::cout << "------------------------------------------------------------------------" << std::endl;

	std::cout << "[Logs]" << std::endl;

	// the rendering loop
	while (window->isOpen())
	{
		// clear the window with black color
		if (borderless) {
			window->clear(sf::Color::Transparent);
		}
		else {
			window->clear(sf::Color::Black);
		}

		// protect access to variables of external threads
		mutex.lock();

		// draw everything here...
		size_t size = frequencies.size();
		std::vector<double> inter_f = std::vector<double>();
		if (inter && size > 1) {
			for (double i = 0; i <= size - 1; i += (double)size / WIDTH) {
				inter_f.push_back(interpolate(i));
			}
			size = inter_f.size();
		}
		double max = 1;
		for (int i = 0; i < size; i++) {
			double magnitude = inter && size > 1 ? inter_f[i] : frequencies[i];
			double peak = delayedPeaks && !inter ? peaks[i] : 0;
			sf::RectangleShape rectangle = sf::RectangleShape();
			if (magnitude > max) {
				max = magnitude;
			}
			if (magnitude < HEIGHT * 0.0083) {
				magnitude = HEIGHT * 0.0083;
			}
			else if (magnitude > HEIGHT * 0.86) {
				magnitude = HEIGHT * 0.86;
			}
			if (delayedPeaks) {
				if (peak < HEIGHT * 0.0083) {
					peak = HEIGHT * 0.0083;
				}
				else if (peak > HEIGHT * 0.86) {
					peak = HEIGHT * 0.86;
				}
			}
			double margin_x = WIDTH * 0.035;
			double margin_y = HEIGHT * 0.07;
			double barWidth = (WIDTH - 2 * margin_x) / size;
			double shader = shadingRatio * (1 - magnitude / (HEIGHT * 0.86));
			if (shader > 1.0) { shader = 1; }
			sf::Color colour;
			colour = gradient[(int)floor(colourCounter + colourOffset * (1 - magnitude / (HEIGHT * 0.86))) % (256 * 6)];
			colour.r -= colour.r * shader;
			colour.g -= colour.g * shader;
			colour.b -= colour.b * shader;
			rectangle.setFillColor(colour);
			if (classic) {
				double division = HEIGHT * 0.86 / divisions;
				int height = floor(magnitude / division);
				rectangle.setSize(sf::Vector2f(barWidth * (inter ? 1 : gapRatio), 0 - (HEIGHT * 0.86 / divisions) * 0.8));
				for (int j = 0; j < height; j++) {
					rectangle.setPosition(i * barWidth + margin_x + ((1 - (inter ? 1 : gapRatio)) * barWidth / size / 2), HEIGHT - margin_y - j * division);
					window->draw(rectangle);
				}
			}
			else {
				rectangle.setSize(sf::Vector2f(barWidth * (inter ? 1 : gapRatio), -magnitude));
				rectangle.setPosition(i * barWidth + margin_x + ((1 - (inter ? 1 : gapRatio)) * barWidth / size / 2), HEIGHT - margin_y);
				window->draw(rectangle);
				if (delayedPeaks) {
					rectangle.setFillColor(sf::Color::White);
					rectangle.setSize(sf::Vector2f(barWidth * (inter ? 1 : gapRatio), HEIGHT * 0.0083));
					rectangle.setPosition(i * barWidth + margin_x + ((1 - (inter ? 1 : gapRatio)) * barWidth / size / 2), HEIGHT - margin_y - peak);
					window->draw(rectangle);
				}
			}
		}

		if (colourCounter >= 256.0 * 6.0)
		{
			colourCounter = 0;
		}
		else {
			colourCounter += colourChange;
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
						scale2 *= 1.7;
						std::cout << "[+] Logarithmic Scale: " << scale2 << std::endl;
					}
				}
				else if (averageMax > HEIGHT * 0.7 || max > HEIGHT * 0.85) {
					averageMax = HEIGHT / 2;
					if (scale2 > 1e-12) {
						scale2 /= 1.7;
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

void loadSettings() {
	std::string temp;
	std::ifstream file;
	file.open("settings.txt");
	if (file) {
		file >> temp;
		if (temp != version) { return; }
		file >> bars;
		file >> autoScaleCount;
		file >> maxFrequency;
		file >> peakDecay;
		file >> scale1;
		file >> scale2;
		file >> smoothing;
		file >> colourChange;
		file >> shadingRatio;
		file >> colourCounter;
		file >> colourOffset;
		file >> gapRatio;
		file >> autoScale;
		file >> delayedPeaks;
		file >> decaySmoothing;
		file >> classic;
		file >> divisions;
		file >> inter;
		file.close();
		std::cout << "Settings Loaded" << std::endl;
	}
	else {
		std::cout << "Settings Not Found" << std::endl;
	}
}

void saveSettings() {
	std::ofstream file;
	file.open("settings.txt");
	file.clear();
	file << version << std::endl;
	file << bars << std::endl;
	file << autoScaleCount << std::endl;
	file << maxFrequency << std::endl;
	file << peakDecay << std::endl;
	file << scale1 << std::endl;
	file << scale2 << std::endl;
	file << smoothing << std::endl;
	file << colourChange << std::endl;
	file << shadingRatio << std::endl;
	file << colourCounter << std::endl;
	file << colourOffset << std::endl;
	file << gapRatio << std::endl;
	file << autoScale << std::endl;
	file << delayedPeaks << std::endl;
	file << decaySmoothing << std::endl;
	file << classic << std::endl;
	file << divisions << std::endl;
	file << inter;
	file.close();
	std::cout << "Settings Saved" << std::endl;
}

int main() {
	HWND console = GetConsoleWindow();
	RECT r;
	GetWindowRect(console, &r); //stores the console's current dimensions
	MoveWindow(console, r.left, r.top, 650, 700, TRUE);

	loadSettings();

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
	sf::RenderWindow* window = new sf::RenderWindow(sf::VideoMode(WIDTH, HEIGHT), "Audio Visualizer", sf::Style::Default);
	window->setVerticalSyncEnabled(true);
	sf::Image icon;
	icon.loadFromFile("icon.png");
	window->setIcon(icon.getSize().x, icon.getSize().y, icon.getPixelsPtr());
	// deactivate its OpenGL context
	window->setActive(false);

	// resize window
	HWND hwnd = window->getSystemHandle();
	GetWindowRect(hwnd, &r); //stores the console's current dimensions
	MoveWindow(hwnd, r.left, r.top, 1280, 365, TRUE);

	MARGINS margins;
	margins.cxLeftWidth = -1;
	DwmExtendFrameIntoClientArea(window->getSystemHandle(), &margins);

	// launch the rendering thread
	sf::Thread thread(&renderingThread, window);
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
	while (window->isOpen())
	{
		// check all the window's events that were triggered since the last iteration of the loop
		sf::Event event;
		while (window->pollEvent(event))
		{
			// "close requested" event: we close the window
			if (event.type == sf::Event::Closed) {
				window->close();
			}
			else if (event.type == sf::Event::MouseButtonPressed) {
				HWND hwnd = window->getSystemHandle();
				borderless = !borderless;
				if (borderless) {
					SetWindowLongPtr(hwnd, GWL_STYLE, WS_SYSMENU);
					SetWindowPos(hwnd, HWND_TOPMOST, window->getPosition().x, window->getPosition().y + 39, window->getSize().x + 16, window->getSize().y, SWP_SHOWWINDOW);
				}
				else {
					SetWindowLongPtr(hwnd, GWL_STYLE, WS_TILEDWINDOW);
					SetWindowPos(hwnd, HWND_NOTOPMOST, window->getPosition().x, window->getPosition().y - 39, window->getSize().x, window->getSize().y + 39, SWP_SHOWWINDOW);
				}
			}
			else if (event.type == sf::Event::KeyPressed) {
				// protect access to variables of external threads
				mutex.lock();
				if ((sf::Keyboard::isKeyPressed(sf::Keyboard::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::RShift)) && (sf::Keyboard::isKeyPressed(sf::Keyboard::LControl) || sf::Keyboard::isKeyPressed(sf::Keyboard::RControl)) && (sf::Keyboard::isKeyPressed(sf::Keyboard::LAlt) || sf::Keyboard::isKeyPressed(sf::Keyboard::RAlt))) {
					switch (event.key.code) {
					case sf::Keyboard::Up:
						if (divisions < 40) {
							divisions++;
							std::cout << "[+] Classic Divisions: " << divisions << std::endl;
						}
						break;
					case sf::Keyboard::Down:
						if (divisions > 1) {
							divisions--;
							std::cout << "[-] Classic Divisions: " << divisions << std::endl;
						}
						break;
					}
				}
				else if ((sf::Keyboard::isKeyPressed(sf::Keyboard::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::RShift)) && (sf::Keyboard::isKeyPressed(sf::Keyboard::LControl) || sf::Keyboard::isKeyPressed(sf::Keyboard::RControl))) {
					switch (event.key.code) {
					case sf::Keyboard::Up:
						if (colourOffset < 256 * 6) {
							colourOffset++;
							std::cout << "[+] Colour Intensity Offset: " << colourOffset << std::endl;
						}
						break;
					case sf::Keyboard::Down:
						if (colourOffset > 0) {
							colourOffset--;
							std::cout << "[-] Colour Intensity Offset: " << colourOffset << std::endl;
						}
						break;
					case sf::Keyboard::Right:
						if (gapRatio < 1) {
							gapRatio += 0.05;
							std::cout << "[+] Bar Gap Ratio: " << gapRatio << std::endl;
						}
						break;
					case sf::Keyboard::Left:
						if (gapRatio > 0.05) {
							gapRatio -= 0.05;
							std::cout << "[-] Bar Gap Ratio: " << gapRatio << std::endl;
						}
						break;
					}
				}
				else if (sf::Keyboard::isKeyPressed(sf::Keyboard::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::RShift)) {
					switch (event.key.code) {
					case sf::Keyboard::Up:
						if (maxFrequency < 20000 - 50) {
							maxFrequency += 50;
							std::cout << "[+] Max Frequency: " << maxFrequency << std::endl;
						}
						break;
					case sf::Keyboard::Down:
						if (maxFrequency > 50) {
							maxFrequency -= 50;
							std::cout << "[-] Max Frequency: " << maxFrequency << std::endl;
						}
						break;
					case sf::Keyboard::Right:
						if (peakDecay < 30) {
							peakDecay++;
							std::cout << "[+] Peak Decay Speed: " << peakDecay << std::endl;
						}
						break;
					case sf::Keyboard::Left:
						if (peakDecay > 1) {
							peakDecay--;
							std::cout << "[-] Peak Decay Speed: " << peakDecay << std::endl;
						}
						break;
					case sf::Keyboard::BackSpace:
						decaySmoothing = !decaySmoothing;
						if (decaySmoothing) {
							std::cout << "[=] Smoothing Mode: Decay-Only" << std::endl;
						}
						else {
							std::cout << "[=] Smoothing Mode: Normal" << std::endl;
						}
					}
				}
				else if (sf::Keyboard::isKeyPressed(sf::Keyboard::LControl) || sf::Keyboard::isKeyPressed(sf::Keyboard::RControl)) {
					switch (event.key.code) {
					case sf::Keyboard::Up:
						if (bars < 2048) {
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
					case sf::Keyboard::Enter:
						classic = !classic;
						if (classic) {
							std::cout << "[=] Display Mode: Classic" << std::endl;
						}
						else {
							std::cout << "[=] Display Mode: Normal" << std::endl;
						}
						break;
					}
				}
				else if (sf::Keyboard::isKeyPressed(sf::Keyboard::LAlt) || sf::Keyboard::isKeyPressed(sf::Keyboard::RAlt)) {
					switch (event.key.code) {
					case sf::Keyboard::Up:
						if (colourChange < 30) {
							colourChange += 0.5;
							std::cout << "[+] Hue Shift Speed: " << colourChange << std::endl;
						}
						break;
					case sf::Keyboard::Down:
						if (colourChange > 0) {
							colourChange -= 0.5;
							if (colourChange < 0.4) {
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
					case sf::Keyboard::BackSpace:
						inter = !inter;
						if (inter) {
							std::cout << "[+] Interpolation Mode Enabled" << std::endl;
						}
						else {
							std::cout << "[-] Interpolation Mode Disabled" << std::endl;
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
					case sf::Keyboard::BackSpace:
						delayedPeaks = !delayedPeaks;
						if (delayedPeaks) {
							std::cout << "[+] Delayed Peaks: Enabled" << std::endl;
						}
						else {
							std::cout << "[-] Delayed Peaks: Disabled" << std::endl;
						}
						break;
					}
				}

				mutex.unlock();
			}
		}
	}

	recorder.stop();
	saveSettings();

	return 0;
}