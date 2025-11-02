/**
 * Placeholder for a task
 */

#include<thread>
#include<chrono>

class Task {
  int mTaskId;

public:

  Task() : mTaskId(-1) {}
  Task(int id) : mTaskId(id) {}

  inline const int id() const {
     return mTaskId;
  }

  inline void perform() {
     // arbitrary to add some realism that work is not weightless
     std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

};

