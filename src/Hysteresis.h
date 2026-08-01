#ifndef PERSE_COMMON_HYSTERESIS_H
#define PERSE_COMMON_HYSTERESIS_H

#include <initializer_list>
#include <vector>

class Hysteresis {
public:

	/**
	 * @param thresholds Ordered low to high. Should include min and max values
	 * count(levels) = count(thresholds)-1
	 */
	Hysteresis(std::initializer_list<int> thresholds, int margin);

	int get() const;

	int update(int val);
	int reset(int val = 0);

private:
	const std::vector<int> Thresholds;
	const int LevelCount;
	const int Margin;

	int currentLevel;

	int findLevel(int val);

};

#endif //PERSE_COMMON_HYSTERESIS_H
