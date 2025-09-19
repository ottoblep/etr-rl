//
// Created by Linus on 09.04.2022.
//


#include "rayNEAT.h"


bool operator==(Node_Gene a, Node_Gene b){
    return a.id == b.id;
}

bool operator<(Node_Gene a, Node_Gene b){
    return a.id < b.id;
}

bool operator>(Node_Gene a, Node_Gene b){
    return a.id > b.id;
}