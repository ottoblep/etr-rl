//
// Created by Linus on 10.04.2022.
//

#include "rayNEAT.h"



vector<string> split(const string &string_to_split, const string &delimiter) {
    vector<string> res;
    auto start = 0U;
    auto end = string_to_split.find(delimiter);
    while (end != std::string::npos) {
        res.push_back(string_to_split.substr(start, end - start));
        start = end + delimiter.length();
        end = string_to_split.find(delimiter, start);
    }
    res.push_back(string_to_split.substr(start, end));
    //if the string ends with a delimiter, do not include an empty string
    if (res[res.size() - 1].empty()) res.pop_back();
    return res;
}



float rnd_f(float lo, float hi) {
    return lo + static_cast <float> (rand()) / (static_cast <float> (RAND_MAX / (hi - lo)));
}

int GetRandomValue(int lo, int hi) {
    return lo + rand() % (hi - lo + 1);
}