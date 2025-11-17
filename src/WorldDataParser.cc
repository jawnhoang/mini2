#include "../include/WorldDataParser.hpp"
#include <iostream>
#include <string>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif

#include <algorithm>
#include <iomanip>
#include <cctype>
#include <filesystem>
#include <utility>
#include <fstream>
#include <sstream>

using namespace std;

//remove threading
void WorldDataParser::calculateAvgPop1930_1968(vector<vector<string>>& csvData, vector<int>& columnIdx, int& rowStart){
    //get Subset Data
    int totYears = columnIdx[2] - columnIdx[1] + 1;
    // #pragma omp parallel num_threads(num_threads)
    // {
    //     vector<pair<string,float>> individualThreadStorage;
    //     #pragma omp for
    for(int row = rowStart ; row < csvData.size(); ++row){
        // cout << csvData[rowStart][columnIdx[2]] << endl;
        long long result = 0;
        for(int cell = columnIdx[1]; cell <= columnIdx[2]; ++cell){

            if(cell >= csvData[row].size()){
                continue;
            }

            string trim = csvData[row][cell];
            if(trim.back() == '\r'){
                trim.pop_back();
            }
            trim.erase(remove(trim.begin(), trim.end(), '"'), trim.end());
            if(!trim.empty()){
                // cout<<trim<<endl;
                try{
                    result += stoll(trim);
                }catch(...){
                    continue;
                }
            }
        }
        float avg = static_cast<float>(result) / totYears;
        // individualThreadStorage.emplace_back(csvData[row][columnIdx[0]], avg);

        // #pragma omp critical
        // CountryToAvgPop.insert(CountryToAvgPop.end(), individualThreadStorage.begin(), individualThreadStorage.end());
        CountryToAvgPop.emplace_back(csvData[row][columnIdx[0]], avg);
    }

}

void WorldDataParser::printAvgPopSummary(){
    //Order countries by alpha
    sort(CountryToAvgPop.begin(), CountryToAvgPop.end(), 
        [](const auto& a, const auto&b){
            return a.first < b.first;
        });
    cout<< "\nAverage Population of Every Country from 1960 to 2023: " <<endl;
    cout<<"---------------------"<<endl;
    for(const auto& e : CountryToAvgPop){
        cout<< fixed << setprecision(2) << e.first << right << setw(30) << (e.second) << endl;
    }
}

vector<vector<string>> WorldDataParser::read(const string& filePath) {
    ifstream file(filePath);
    if(!file.is_open()){
        throw runtime_error("Failed to open file " + filePath);
    }
    cout << "File found and reading..." << endl;

    string line;
    // vector<string> vectorOfLines;
    // while (getline(file, line)) {
    //     vectorOfLines.push_back(line);
    // }

    vector<vector<string>> data;


    // #pragma omp parallel for num_threads(1) // change to only utilize 1 thread
    // for (int i = 0; i < vectorOfLines.size(); i++){
    //     stringstream ss(vectorOfLines[i]);
        
    //     string cell;
    //     vector<string> row;

    while (getline(file, line)) {
        vector<string> row;
        stringstream ss(line);
        string cell;
        while (getline(ss, cell, ',')) {
            if (!cell.empty() && cell.back() == '\r') cell.pop_back();
            row.push_back(cell);
        }
        // Strip BOM from first cell
        if (!data.size() && !row.empty()) {
            if (row[0].size() >= 3 &&
                (unsigned char)row[0][0] == 0xEF &&
                (unsigned char)row[0][1] == 0xBB &&
                (unsigned char)row[0][2] == 0xBF) {
                row[0].erase(0, 3);
            }
        }
        if(!row.empty()){
            data.push_back(std::move(row));
        }
    }
    return data;
}

const vector<pair<string,float>>& WorldDataParser::getCountryToAvgPop() const{
    return CountryToAvgPop;
}