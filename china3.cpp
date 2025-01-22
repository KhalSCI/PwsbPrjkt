#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <chrono>
#include <memory>
#include <atomic>
#include <iostream>

using namespace std;
using namespace std::chrono_literals;

mutex cout_mtx;

class DynamicSemaphore {
private:
    int count;
    const int max_count;
    mutex mtx;
    condition_variable cv;
public:
    DynamicSemaphore(int max) : max_count(max), count(0) {}
    
    void initialize() {
        unique_lock<mutex> lock(mtx);
        count = max_count;
        cv.notify_all();
    }
    
    void acquire() {
        unique_lock<mutex> lock(mtx);
        cv.wait(lock, [this] { return count > 0; });
        count--;
    }
    
    bool try_acquire() {
        unique_lock<mutex> lock(mtx);
        if (count > 0) {
            count--;
            return true;
        }
        return false;
    }
    
    void release(int releases = 1) {
        unique_lock<mutex> lock(mtx);
        count = min(count + releases, max_count);
        cv.notify_all();
    }
    
    int available() const {
        return count;
    }
};

int main() {
    const int n = 2;
    vector<int> k = {3, 1};
    vector<int> p = {4, 3};
    const int p0 = 7;
    const int num_elves = 4;

    vector<unique_ptr<DynamicSemaphore>> scaffolding_sems;
    for (int capacity : k) {
        auto sem = make_unique<DynamicSemaphore>(capacity);
        sem->initialize();
        scaffolding_sems.push_back(move(sem));
    }

    vector<unique_ptr<DynamicSemaphore>> ornament_sems;
    for (int ornaments : p) {
        auto sem = make_unique<DynamicSemaphore>(ornaments);
        sem->initialize();
        ornament_sems.push_back(move(sem));
    }

    mutex level0_mtx;
    int level0_ornaments = 0;
    condition_variable level0_cv;
    DynamicSemaphore ornament_sem_level0(p0);
    ornament_sem_level0.initialize();

    atomic<bool> all_levels_full(false);

    thread santa([&]() {
        while (true) {
            unique_lock<mutex> lock(level0_mtx);
            level0_cv.wait(lock, [&] { 
                return level0_ornaments < p0 || all_levels_full.load(); 
            });

            if (all_levels_full.load()) break;

            int add = max(0, p0 - level0_ornaments);
            level0_ornaments += add;
            ornament_sem_level0.release(add);

            {
                lock_guard<mutex> cout_lock(cout_mtx);
                cout << "🎅 Restocked " << add << " ornaments (Total: " << level0_ornaments << ")\n";
            }

            bool complete = true;
            for (auto& sem : ornament_sems) {
                if (sem->available() > 0) {
                    complete = false;
                    break;
                }
            }
            if (complete) {
                all_levels_full = true;
                level0_cv.notify_all();
                break;
            }
        }
        
        {
            lock_guard<mutex> cout_lock(cout_mtx);
            cout << "🎅 All decoration complete! Shutting down...\n";
        }
    });

    vector<thread> elves;
    for (int elf = 0; elf < num_elves; ++elf) {
        elves.emplace_back([&, elf]() {
            {
                lock_guard<mutex> cout_lock(cout_mtx);
                cout << "🧝 Elf " << elf << " started\n";
            }

            while (!all_levels_full.load()) {
                ornament_sem_level0.acquire();
                
                {
                    unique_lock<mutex> lock(level0_mtx);
                    level0_ornaments--;
                    level0_cv.notify_one();
                }

                bool placed = false;
                for (int i = n-1; i >= 0; --i) {
                    if (ornament_sems[i]->try_acquire()) {
                        vector<int> acquired_levels;
                        try {
                            for (int level = 0; level <= i; ++level) {
                                scaffolding_sems[level]->acquire();
                                acquired_levels.push_back(level);
                                {
                                    lock_guard<mutex> cout_lock(cout_mtx);
                                    cout << "🧝 Elf " << elf << " ↗ Level " << level+1 
                                         << " [Used: " << (k[level] - scaffolding_sems[level]->available()) 
                                         << "/" << k[level] << "]\n";
                                }
                            }

                            {
                                lock_guard<mutex> cout_lock(cout_mtx);
                                cout << "⭐ Elf " << elf << " placed on Level " << i+1 
                                     << " [Total: " << (p[i] - ornament_sems[i]->available()) 
                                     << "/" << p[i] << "]\n";
                            }
                            this_thread::sleep_for(20ms);

                            // Descend with clear tracking
                            for (auto it = acquired_levels.rbegin(); it != acquired_levels.rend(); ++it) {
                                int level = *it;
                                scaffolding_sems[level]->release();
                                if (level > 0) {
                                    lock_guard<mutex> cout_lock(cout_mtx);
                                    cout << "🧝 Elf " << elf << " ↘ Level " << level+1 
                                         << " [Used: " << (k[level] - scaffolding_sems[level]->available()) 
                                         << "/" << k[level] << "]\n";
                                }
                            }
                            // Explicit return to Level 0
                            {
                                lock_guard<mutex> cout_lock(cout_mtx);
                                cout << "🧝 Elf " << elf << " returned to Level 0\n";
                            }
                            placed = true;
                        }
                        catch (...) {}
                        break;
                    }
                }

                if (!placed) {
                    {
                        unique_lock<mutex> lock(level0_mtx);
                        level0_ornaments++;
                        level0_cv.notify_one();
                    }
                    ornament_sem_level0.release();
                }
            }
            
            {
                lock_guard<mutex> cout_lock(cout_mtx);
                cout << "🧝 Elf " << elf << " finished\n";
            }
        });
    }

    santa.join();
    for (auto& elf : elves) {
        elf.join();
    }

    return 0;
}