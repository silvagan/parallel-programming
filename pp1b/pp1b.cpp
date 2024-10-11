#include <iostream>
#include <string>
#include <mutex>
#include <vector>
#include <thread>
#include <sstream>
#include <fstream>
#include <nlohmann/json.hpp>
#include <regex>
#include <algorithm>
#include <chrono>
#include <omp.h>

using namespace std;
using json = nlohmann::json;

string input = "IFF_2-1_KondrataviciusK_L1b_dat_3.json";
string output = "IFF_2-1_KondrataviciusK_L1b_rez.txt";

const int thcount = 16;

struct Package {
    string elementName;
    double weight;
    double price;
    Package() {}
    Package(string name, double w, double p) : elementName(name), weight(w), price(p) {}
};

struct Result {
    Package original;
    double calculated;
    Result(Package p, double r) : original(p), calculated(r) {}
};

double f1(Package data) {
    double result = 0.0;
    int n = 100; // Fixed size to ensure O(n^3) complexity

    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            for (int k = 0; k < n; ++k) {
                // Simulate some work
                result += ((data.price - data.weight)) / (data.price + data.weight + 1);
            }
        }
    }
    this_thread::sleep_for(chrono::milliseconds(100));

    return result;
}

Package fromJson(const json& j) {
    Package data;
    data.elementName = j.at("elementName").get<string>();
    data.weight = j.at("weight").get<double>();
    data.price = j.at("price").get<double>();
    return data;
}

const char separator = ' ';

bool comparator(const Result& a, const Result& b) {
    return a.calculated < b.calculated;
}

int main() {
    auto start = chrono::high_resolution_clock::now();

    omp_set_num_threads(thcount);

    vector<Package> packages;
    ifstream file(input);
    json data = json::parse(file);
    for (const auto& it : data) {
        packages.push_back(fromJson(it));
    }

    int num_threads = thcount;
    vector<Result> results;
    double weight_sum = 0.0;
    double price_sum = 0.0;

#pragma omp parallel num_threads(num_threads)
    {
        int thread_id = omp_get_thread_num();
        cout << "Thread "+ to_string(thread_id) + " is running\n";
        int num_elements = packages.size();
        int elements_per_thread = num_elements / num_threads;
        int start = thread_id * elements_per_thread;
        int end = (thread_id == num_threads - 1) ? num_elements : start + elements_per_thread;

        vector<Result> local_results;
        double local_weight_sum = 0.0;
        double local_price_sum = 0.0;

        for (int i = start; i < end; ++i) {
            double result = f1(packages[i]);
            if (result > 0) {
                local_results.emplace_back(packages[i], result);
                local_weight_sum += packages[i].weight;
                local_price_sum += packages[i].price;
            }
        }

#pragma omp critical
        {
            for (const auto& res : local_results) {
                auto pos = lower_bound(results.begin(), results.end(), res, comparator);
                results.insert(pos, res);
            }
            weight_sum += local_weight_sum;
            price_sum += local_price_sum;
        }
    }

    ofstream resfile(output);
    if (resfile.is_open()) {
        if (results.size() > 0)
        {
            resfile << left << setw(15) << setfill(separator) << "element name" << right
                << setw(8) << setfill(separator) << "weight"
                << setw(8) << setfill(separator) << "price"
                << setw(15) << setfill(separator) << "result" << "\n";
            for (size_t i = 0; i < results.size(); ++i) {
                resfile << left << setw(15) << setfill(separator) << results[i].original.elementName << right
                    << setw(8) << setfill(separator) << results[i].original.weight
                    << setw(8) << setfill(separator) << results[i].original.price
                    << setw(15) << setfill(separator) << results[i].calculated << "\n";
            }
            resfile << "Sum of weight fields: " << weight_sum << "\n";
            resfile << "Sum of price fields: " << price_sum << "\n";
            resfile.close();
        }
        else
            resfile << "there are no results" << "\n";
    }
    else {
        cout << "Unable to open file";
    }

    auto end = chrono::high_resolution_clock::now();
    chrono::duration<double> elapsed = end - start;
    cout << "Elapsed time: " << elapsed.count() << " seconds\n";

    return 0;
}