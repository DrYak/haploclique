/* Copyright 2012-2014 Tobias Marschall and Armin Töpfer
 *
 * This file is part of HaploClique.
 *
 * HaploClique is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HaploClique is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HaploClique.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <math.h>
#include <boost/math/distributions/normal.hpp>
#include <stdlib.h>
#include <map>
#include <set>
#include <algorithm>
#include <vector>
#include <boost/algorithm/string.hpp>
#include <boost/tokenizer.hpp>

#include "NewEdgeCalculator.h"

 using namespace std;
 using namespace boost;

 const double NewEdgeCalculator::FRAME_SHIFT_WEIGHT = 0.01;
 

NewEdgeCalculator::NewEdgeCalculator(double Q, double edge_quasi_cutoff, double overlap, bool frameshift_merge, map<int, double>& simpson_map, double edge_quasi_cutoff_single, double overlap_single, double edge_quasi_cutoff_mixed) {
    this->Q = Q;
    this->EDGE_QUASI_CUTOFF = edge_quasi_cutoff;
    this->EDGE_QUASI_CUTOFF_SINGLE = edge_quasi_cutoff_single;
    this->EDGE_QUASI_CUTOFF_MIXED = edge_quasi_cutoff_mixed;
    this->MIN_OVERLAP_CLIQUES = overlap;
    this->MIN_OVERLAP_SINGLE = overlap_single;
    this->FRAMESHIFT_MERGE = frameshift_merge;
    this->SIMPSON_MAP = simpson_map;
}

NewEdgeCalculator::~NewEdgeCalculator() {
}

std::vector<int> NewEdgeCalculator::commonPositions(const AlignmentRecord::covmap & cov_ap1, const AlignmentRecord::covmap & cov_ap2) const{
    std::vector<int> res;
    for (auto it = cov_ap1.begin(); it != cov_ap1.end(); ++it){
            auto fit = cov_ap2.find(it->first);
            if (fit!= cov_ap2.end()){
                res.push_back(it->first);
            }
    }
    return res;
}

std::vector<int> NewEdgeCalculator::tailPositions(const AlignmentRecord::covmap & cov_ap1, const AlignmentRecord::covmap & cov_ap2) const{
    std::vector<int> res;
    for (auto it = cov_ap1.begin(); it != cov_ap1.end(); ++it){
        auto fit = cov_ap2.find(it->first);
        if (fit == cov_ap2.end()){
            res.push_back(it->first);
        }
    }
    for (auto it = cov_ap2.begin(); it != cov_ap2.end(); ++it){
        auto fit = cov_ap1.find(it->first);
        if (fit == cov_ap1.end()){
            res.push_back(it->first);
        }
    }
    std::sort(res.begin(),res.end());
    return res;
}

double NewEdgeCalculator::qScore(const AlignmentRecord::mapValue& value, char& x) const{
    if (value.base == x){
        return 1.0 - std::pow(10, (double)(-value.qual-33)/10.0);
    } else {
        return std::pow(10, (double)(-value.qual - 33)/10.0)/3.0;
    }
}

double NewEdgeCalculator::calculateProbM(const std::vector<int> & aub, const AlignmentRecord::covmap & cov_ap1, const AlignmentRecord::covmap & cov_ap2) const{
    double res = 1.0;
    double sum = 0.0;
    std::string bases = "ACTG";
    for (auto i : aub){
        sum = 0.0;
        for (char j : bases){
            sum += qScore(cov_ap1.at(i),j)*qScore(cov_ap2.at(i),j);
        }
        res *= sum;
    }
    return res;
}

//TO DO: calculate allel frequency distribution beforehand
double NewEdgeCalculator::calculateProb0(const std::vector<int> & tail) const{
    double res = 1.0;
    for(auto i : tail){
        //simpson_map is 1-based
        auto k = this->SIMPSON_MAP.find(i);
        if (k != this->SIMPSON_MAP.end()){
            res *= k->second;
        } else {
            res *= 0.25;
        }
    }
    return res;
}

//TO DO: find out whether gaps / insertions are compatible
bool NewEdgeCalculator::checkGaps(const AlignmentRecord::covmap & cov_ap1,const AlignmentRecord::covmap & cov_ap2,const std::vector<int> & aub) const{
    for (int i = 0; i < (signed)aub.size()-1; ++i){
        int ref_diff = aub[i+1]-aub[i];
        int pos_diff1 = cov_ap1.at(aub[i+1]).pir-cov_ap1.at(aub[i]).pir; //<0 if jump is given
        int pos_diff2 = cov_ap2.at(aub[i+1]).pir-cov_ap2.at(aub[i]).pir; //<0 if jump is given
        bool jump1 = cov_ap1.at(aub[i+1]).read-cov_ap1.at(aub[i]).read; //=0/1 for no jump/jump
        bool jump2 = cov_ap2.at(aub[i+1]).read-cov_ap2.at(aub[i]).read;
        //insertion
        if(ref_diff == 1 && pos_diff1 != pos_diff2 && (jump1 || jump2) == 0){
            return false;
        }
        //deletion
        else if(ref_diff > 1 && pos_diff1 != pos_diff2 && (jump1 || jump2) == 0){
            return false;
        }
        else if(ref_diff > 1 && pos_diff1 == pos_diff2 && pos_diff1 > 1){
            return false;
        }
    }
    if (aub.size()<1) return false;
    return true;
}

bool NewEdgeCalculator::similarityCriterion(const AlignmentRecord & a1, const AlignmentRecord::covmap & cov_ap1, const AlignmentRecord & a2, const AlignmentRecord::covmap & cov_ap2, std::vector<int> & aub, std::vector<int> tail) const{

    //Threshold for probability that reads were sampled from same haplotype
    double cutoff = 0;
    if (a1.getName().find("Clique") != string::npos && a2.getName().find("Clique") != string::npos) {
        cutoff = this->EDGE_QUASI_CUTOFF;
    } else if (a1.getName().find("Clique") != string::npos || a2.getName().find("Clique") != string::npos) {
        cutoff = this->EDGE_QUASI_CUTOFF_MIXED;
    } else {
        cutoff = this->EDGE_QUASI_CUTOFF_SINGLE;
    }

    //Threshold for Overlap of Read Alignments
    double MIN_OVERLAP = 0;
    if (a1.getName().find("Clique") != string::npos && a2.getName().find("Clique") != string::npos) {
        MIN_OVERLAP = MIN_OVERLAP_CLIQUES;
    } else {
        MIN_OVERLAP = MIN_OVERLAP_SINGLE;
    }
    if (aub.size()<=MIN_OVERLAP*std::min(cov_ap1.size(),cov_ap2.size())) return false;
    double p_m = calculateProbM(aub, cov_ap1, cov_ap2);
    double p_0 = calculateProb0(tail);
    double prob = p_m*p_0;
    int test = aub.size()+tail.size();
    double potence = 1.0/test;
    double final_prob = std::pow(prob,potence);
    //cout << "Final prob: " << final_prob << endl;
    return final_prob >= cutoff;
}

bool NewEdgeCalculator::edgeBetween(const AlignmentRecord & ap1, const AlignmentRecord & ap2) const{
    AlignmentRecord::covmap cov_ap1 = ap1.coveredPositions();
    AlignmentRecord::covmap cov_ap2 = ap2.coveredPositions();
    std::vector<int> aub = commonPositions(cov_ap1, cov_ap2);
    if (aub.size() == 0 || (!checkGaps(cov_ap1, cov_ap2, aub))){
        return false;
    }
    //cout << "Gaps are compatible" << endl;
    std::vector<int> tail = tailPositions(cov_ap1,cov_ap2);
    //cout << "Tail positions computed" << endl;
    return similarityCriterion(ap1, cov_ap1, ap2, cov_ap2, aub, tail);
}

void NewEdgeCalculator::getPartnerLengthRange(const AlignmentRecord& ap, unsigned int* min, unsigned int* max) const {
    assert(false);
}




