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

using namespace std;
using json = nlohmann::json;

string input = "IFF_2-1_KondrataviciusK_L1_dat_2.json";
string output = "IFF_2-1_KondrataviciusK_L1_rez.txt";

const int dcount = 15;
const int rcount = 100;
const int thcount = 10;

struct Package
{
public:
    string elementName;
    double weight;
    double price;
    Package() {}
    Package(string name, double w, double p)
    {
        elementName = name;
        weight = w;
        price = p;
    }
};

std::atomic<bool> readingEnd(false);

class DataMonitor
{
    mutex mtx;
    condition_variable cv;
    Package pack[dcount];
public:
    int workingIndex = 0;

    void addItem(Package toAdd) {

        unique_lock<mutex> lock(mtx);
        cv.wait(lock, [this] { return workingIndex < dcount; });
        pack[workingIndex] = toAdd;
        workingIndex++;
        cout << "after adding = " + to_string(workingIndex) + "\n";
        lock.unlock();
        cv.notify_one();
    }
    Package removeItem() {
        unique_lock<mutex> lock(mtx);
        cv.wait(lock, [this] { return workingIndex > 0 || readingEnd; });
        Package data;
        if (workingIndex > 0) {
            data = pack[workingIndex - 1];
            pack[workingIndex - 1] = Package(); // Clear the package
            workingIndex--;
            cout << "after removing = " + to_string(workingIndex) + "\n";
        }
        cv.notify_one();
        return data;
    }

    void notify_all_threads()
    {
        cv.notify_all();
    }
};

double f1(Package data)
{
    double result = 0.0;
    int n = 100; // Fixed size to ensure O(n^3) complexity

    for (int i = 0; i < n; ++i)
    {
        for (int j = 0; j < n; ++j)
        {
            for (int k = 0; k < n; ++k)
            {
                // Simulate some work
                result += ((data.price - data.weight)) / (data.price + data.weight +1);
            }
        }
    }
    this_thread::sleep_for(chrono::milliseconds(100));

    return result;
}

struct Result
{
public:
    Package original;
    double computed;
    Result() {};
    Result(Package data, double result) {
        original = data;
        computed = result;
    }
};

struct Results
{
public:
    Result results[rcount];
    Results(Result all[], int size) 
    {
        for (int i = 0; i < size; i++) {
            results[i] = all[i];
        }
    }
};

class ResultMonitor
{
    Result results[rcount];
    mutex mtx;

public:
    int workingIndex = 0;

    static bool comparator(const Result& a, const Result& b) {
        return a.computed < b.computed;
    }

    void addItem(Result toAdd) 
    {
        unique_lock<mutex> lock(mtx);
        if (workingIndex < rcount) {
            // Find the correct position using binary search
            auto pos = lower_bound(results, results + workingIndex, toAdd, comparator);

            // Calculate the index position
            int index = pos - results;

            // Shift elements to the right
            for (int i = workingIndex; i > index; --i) {
                results[i] = results[i - 1];
            }

            // Insert the new item
            results[index] = toAdd;
            workingIndex++;
        }
    }

    Results getItems()
    {
        unique_lock<mutex> lock(mtx);
        return Results(results, workingIndex);
    }
};

Package fromJson(const json& j)
{
    Package data;
    data.elementName = j.at("elementName").get<string>();
    data.weight = j.at("weight").get<double>();
    data.price = j.at("price").get<double>();
    return data;
}

const char separator = ' ';

std::atomic<int> activeWorkerThreads(0);
std::condition_variable allWorkersDone;
std::mutex allWorkersDoneMtx;

void MainThread(DataMonitor& monitor, ResultMonitor& rm) 
{
    ifstream file(input);
    json data = json::parse(file);
    for (const auto& it : data) {
        monitor.addItem(fromJson(it));
    }
    readingEnd = true;
    monitor.notify_all_threads();

    unique_lock<mutex> lock(allWorkersDoneMtx);
    allWorkersDone.wait(lock, [] { return activeWorkerThreads == 0; });

    Results res = rm.getItems();
    lock.unlock();

    ofstream resfile(output);

    if (resfile.is_open())
    {
        if (rm.workingIndex > 0)
        {
            resfile << left << setw(15) << setfill(separator) << "element name" << right
                << setw(8) << setfill(separator) << "weight"
                << setw(8) << setfill(separator) << "price"
                << setw(15) << setfill(separator) << "result" << "\n";
            for (int i = 0; i < rm.workingIndex; ++i) {
                resfile << left << setw(15) << setfill(separator) << res.results[i].original.elementName << right
                    << setw(8) << setfill(separator) << res.results[i].original.weight
                    << setw(8) << setfill(separator) << res.results[i].original.price
                    << setw(15) << setfill(separator) << res.results[i].computed << "\n";
            }
        }
        else
        {
            resfile << "there are no results";
        }

        resfile.close();
    }
    else cout << "Unable to open file";
    cout << "Main thread is done\n";
}

void WorkerThread(DataMonitor& dataMonitor, ResultMonitor& resultMonitor)
{
    stringstream ss;
    ss << this_thread::get_id();

    cout << "Worker thread is starting:" + ss.str() + "\n";
    activeWorkerThreads++;
    while (true)
    {
        if (readingEnd && dataMonitor.workingIndex == 0)
        {
            break;
        }
        
        Package data = dataMonitor.removeItem();
        double result = f1(data);
        if (result > 0 && data.elementName != "")
        {
            cout << "w " + ss.str() + " add item to rm " + to_string(resultMonitor.workingIndex) + "\n";
            resultMonitor.addItem(Result(data, result));
            cout << "res: " + to_string(resultMonitor.workingIndex) + "\n";
        }
    }
    activeWorkerThreads--;
    cout << "Worker thread is done:" + ss.str() + "\n";
    if (activeWorkerThreads == 0) {
        allWorkersDone.notify_one();
    }
}

int main() 
{
    DataMonitor dm;
    ResultMonitor rm;
    vector<thread> threads;

    auto start = chrono::high_resolution_clock::now();

    threads.push_back(thread(MainThread, ref(dm), ref(rm)));

    for (int i = 0; i < thcount; ++i) {
        threads.push_back(thread(WorkerThread, ref(dm), ref(rm)));
    }

    for (auto& th : threads) {
        th.join();
    }

    auto end = chrono::high_resolution_clock::now();
    chrono::duration<double> elapsed = end - start;
    cout << "Elapsed time: " << elapsed.count() << " seconds\n";

    return 0;
}