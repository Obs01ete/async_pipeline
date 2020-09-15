#include <iostream>
#include <thread>
#include <chrono>
#include <future>
#include <vector>
#include <deque>


std::pair<size_t, std::string> func1(std::future<std::pair<size_t, std::string>>&& fut_s)
{
    const auto s = fut_s.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(900));
    std::string result(s.second + " func1");
    return std::make_pair(s.first, result);
}


std::pair<size_t, std::string> func2(std::future<std::pair<size_t, std::string>>&& fut_s)
{
    const auto s = fut_s.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(950));
    std::string result(s.second + " func2");
    return std::make_pair(s.first, result);
}


void visualize(std::future<std::pair<size_t, std::string>>&& fut_s,
    const std::chrono::time_point<std::chrono::high_resolution_clock>& start_time,
    std::atomic<unsigned long>& current_idx)
{
    const auto s = fut_s.get();
    size_t this_idx = s.first;
    {
        while (this_idx != current_idx.load())
        {
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }

        std::cout << "Sample " << this_idx << " result: " << s.second << " finished at " <<
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - start_time).count() << std::endl;

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        current_idx.store(current_idx.load() + 1);
    }
}



int main()
{
    const auto start_time = std::chrono::high_resolution_clock::now();

    size_t pipeline_depth = 2; // Number of processors except visualizer

    std::deque<std::future<void>> visualize_futures;
    std::atomic<unsigned long> current_vis_idx { 0 };

    for (size_t i = 0; i < 100; i++)
    {
        auto input_str = std::string("Input String ") + std::to_string(i);
        std::promise<std::pair<size_t, std::string> > p0;
        auto f0 = p0.get_future();
        p0.set_value(std::make_pair(i, input_str));

        auto f1 = std::async(std::launch::async, &func1, std::move(f0));

        auto f2 = std::async(std::launch::async, &func2, std::move(f1));

        auto f3 = std::async(std::launch::async, &visualize, std::move(f2),
            std::ref(start_time), std::ref(current_vis_idx));

        visualize_futures.push_back(std::move(f3));
        if (visualize_futures.size() > pipeline_depth)
        {
            // At this point the main thread will call a destructor of the oldest future,
            // which in its turn will block until the thread gets joined.
            visualize_futures.pop_front();
        }

        std::cout << "Enqueued sample: " << i << std::endl;
    }

    std::cout << "Waiting to finish..." << std::endl;
    for (auto& fut : visualize_futures)
    {
        fut.get();
    }

    std::cout << "Finished!" << std::endl;
    return 0;
}
