#pragma once

#include <string>

std::string perform_proof_of_work(const std::string &challenge, unsigned int difficulty);
bool verify_proof_of_work(const std::string &challenge, const std::string &proof, unsigned int difficulty);

bool proof_of_work_test();