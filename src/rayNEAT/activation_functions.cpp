//
// Created by Linus on 11.04.2022.
//


#include "rayNEAT.h"



float sigmoid(float x) {
    return 1.f / (1.f + expf(-4.9f * x));
}

[[maybe_unused]] float relu(float x){
    return std::max(0.f, x);
}

float heavyside(float x){
    return x > 0 ? 1.f : 0.f;
}

float softplus(float x){
    return log(1 + exp(x));
}


float gaussian(float x){
    return exp(-1.f * x * x);
}