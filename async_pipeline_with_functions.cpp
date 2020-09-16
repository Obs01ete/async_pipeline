// Tutorial on multithreading with std::async
// Dmitrii Khizbullin, 2020
//
// This example shows how to create a pipeline of functions executed in different threads.


#include <iostream>
#include <thread>
#include <chrono>
#include <future>
#include <deque>


std::pair<size_t, std::string> func1(std::future<std::pair<size_t, std::string>>&& future_input)
{
    const auto input = future_input.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(900));
    std::string output(input.second + " func1");
    return std::make_pair(input.first, output);
}


std::pair<size_t, std::string> func2(std::future<std::pair<size_t, std::string>>&& future_input)
{
    const auto input = future_input.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(950));
    std::string output(input.second + " func2");
    return std::make_pair(input.first, output);
}


void visualize(std::future<std::pair<size_t, std::string>>&& future_input,
    const std::chrono::time_point<std::chrono::high_resolution_clock>& start_time,
    std::atomic<unsigned long>& current_idx)
{
    const auto input = future_input.get();
    size_t this_idx = input.first;

    while (this_idx != current_idx.load())
    {
        std::this_thread::sleep_for(std::chrono::microseconds(1));
    }

    std::cout << "Sample " << this_idx << " output: '" << input.second << "' finished at " <<
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - start_time).count() << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    current_idx.store(current_idx.load() + 1);
}


int main()
{
    const auto start_time = std::chrono::high_resolution_clock::now();

    size_t pipeline_depth = 2; // Number of processors except visualizer

    std::deque<std::future<void>> visualize_futures;

    std::atomic<unsigned long> current_idx { 0 };

    for (size_t idx = 0; idx < 100; idx++)
    {
        auto input_str = std::string("input_string_") + std::to_string(idx);
        std::promise<std::pair<size_t, std::string> > promise_0;
        auto future_0 = promise_0.get_future();
        promise_0.set_value(std::make_pair(idx, input_str));

        auto future_1 = std::async(std::launch::async, &func1, std::move(future_0));

        auto future_2 = std::async(std::launch::async, &func2, std::move(future_1));

        auto future_vis = std::async(std::launch::async, &visualize, std::move(future_2),
            std::ref(start_time), std::ref(current_idx));

        visualize_futures.push_back(std::move(future_vis));
        if (visualize_futures.size() > pipeline_depth)
        {
            // At this point the main thread will call a destructor of the oldest future,
            // which in its turn will block until the thread gets joined.
            visualize_futures.pop_front();
        }

        std::cout << "Enqueued sample: " << idx << std::endl;
    }

    std::cout << "Waiting to finish..." << std::endl;
    for (auto& fut : visualize_futures)
    {
        fut.get();
    }

    std::cout << "Finished!" << std::endl;
    return 0;
}
