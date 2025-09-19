//
// Created by Linus on 19.03.2022.
//


#include "rayNEAT.h"

bool operator==(Connection_Gene a, Connection_Gene b) {
    return a.start == b.start && a.end == b.end;
}

bool operator<(Connection_Gene a, Connection_Gene b) {
    return a.id < b.id;
}

bool operator>(Connection_Gene a, Connection_Gene b) {
    return a.id > b.id;
}