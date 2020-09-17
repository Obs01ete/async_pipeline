// Tutorial on multithreading with std::async
// Dmitrii Khizbullin, 2020
//
// This example shows how to create a pipeline of functions executed in separate threads.


// Here we include std::async, std::future and std::promise as our threading API.
#include <future>
// We need a data structure to manage our pipeline. The double-ended queue fits nicely.
#include <deque>
// We'd like to be able to print to console.
#include <iostream>
// And we want to measure time.
#include <chrono>


// Let's create the first function that performs some heavy processing.
// In fact we are going to emulate processing with a sleep function
// that does not actually load a CPU core.
std::pair<size_t, std::string> func1(std::future<std::pair<size_t, std::string>>&& future_input)
{
    // Here we retrieve payload from the future object.
    // This call will block until the result is produced by another thread.
    const auto input = future_input.get();
    // Let's sleep for a while. This is to be replaced with actual compute eventually.
    std::this_thread::sleep_for(std::chrono::milliseconds(900));
    // Attach a string to input and return it to make sure that the function got executed.
    std::string output(input.second + " func1");
    // We return a regular object which will be turned into a future object by std::async.
    return std::make_pair(input.first, output);
}


// And we create another function that we are going to put into our multithreaded pipeline.
// Notice that we are passing an std::future object into it, and mark it as
// a std::move destination with &&.
std::pair<size_t, std::string> func2(std::future<std::pair<size_t, std::string>>&& future_input)
{
    // Similar to func1, but we emulate different compute time.
    const auto input = future_input.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(950));
    std::string output(input.second + " func2");
    return std::make_pair(input.first, output);
}


// After the processing of a sample is done we would like to visualize the results.
// We are going to do this by printing the result into the console.
// We also want the visualization to be smooth, such that our prints come
// in nice regular intervals.
void visualize(std::future<std::pair<size_t, std::string>>&& future_input,
    const std::chrono::time_point<std::chrono::high_resolution_clock>& start_time,
    std::atomic<unsigned long>& current_idx)
{
    const auto input = future_input.get();
    // It is very important that we've been carrying a sample index throughout the pipeline.
    size_t this_idx = input.first;

    // This is a point when we would like to synchronize our samples.
    // See, multiple processing threads run concurrently, so we need to make sure that
    // they are all aligned sequentially during visualization.
    // Here current_idx atomic keeps track of the current frame to be visualized.
    // Multiple visulaization threads will try to visualize their sample, but only the one
    // which is responsible for the oldest not yet visualized sample will pass this check.
    while (this_idx != current_idx.load())
    {
        // We also do not want threads to be eating CPU wile polling this atomic.
        // So let's add some tiny sleep for the thread to be patient and wait
        // for its turn. 
        std::this_thread::sleep_for(std::chrono::microseconds(1));
    }

    // Time to see what we got and at what timestamp relative to the launch of the app.
    std::cout << "Sample " << this_idx << " output: '" << input.second << "' finished at " <<
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - start_time).count() << std::endl;

    // Since we want our visualization to look smooth and publish results at even time
    // intervals, we block the pipeline for our desired 1 second. This value must be set
    // bigger than the longest of func1 and func2. Any pipeline is as slow as its slowest
    // stage, and we do not want it to be a compute stage. Varying compute time can
    // introduce jitter to our visualization. Sleep is more reliable in this regard
    // as long as we have enough free cores in the CPU.
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Finally after the current sample is visualized, we are free to advance current_idx
    // and allow threads that visualize next samples to run.
    current_idx.store(current_idx.load() + 1);
}


// The entry point to the app.
int main()
{
    // Capture the moment we start the app.
    const auto start_time = std::chrono::high_resolution_clock::now();

    // Here we explicitly specify the number of processors except visualizer.
    size_t pipeline_depth = 2;

    // We need to keep track of visualizer's future objects which essentially
    // return void, however a future object also keeps track of the event of
    // sample processing end. This will allow us to prevent the overflow of
    // the pipeline further below.
    std::deque<std::future<void>> visualize_futures;

    // An aid to guarantee serial visualization of incoming samples is
    // an atomic index variable.
    std::atomic<unsigned long> current_idx { 0 };

    // Now we are ready to fill our pipeline with samples. Let's limit the number
    // of samples to 100 to showcase how we can gracefully stop the pipeline
    // after all samples are visualized.
    for (size_t idx = 0; idx < 100; idx++)
    {
        // Here we create an input to the pipeline. Let's make it a string.
        auto input_str = std::string("input_string_") + std::to_string(idx);
        // We'd like to wrap the input into a future object which is available
        // immediately. This trick allows us to have a uniform signature of
        // functions func#, that takes a future object and returns a value
        // object, which in its turn will be wrapped into a brand new future object
        // on return from std::async.
        std::promise<std::pair<size_t, std::string> > promise_0;
        auto future_0 = promise_0.get_future();
        // Notice that we pass not only the payload to our processors, but an index
        // of a sample. This allows us to figure out the order to visualize samples.
        // Otherwise sample N+1 may be ready earlier than sample N.
        promise_0.set_value(std::make_pair(idx, input_str));

        // Here we go launching our first stage of the pipeline. Notice that we
        // run std::async in std::launch::async mode to immediately start compute.
        // We also have to move the future object since it is not copyable.
        auto future_1 = std::async(std::launch::async, &func1, std::move(future_0));

        // Since the future object from the first stage is available immediately,
        // we launch the second stage right away.
        auto future_2 = std::async(std::launch::async, &func2, std::move(future_1));

        // The visualization stage is also enqueued without delay. This is where
        // asynchronous operations shine. The atomic current_idx is passed here by
        // reference to ensure sequential visualisation of samples despite multiple
        // visualization threads will be running concurrently.
        auto future_vis = std::async(std::launch::async, &visualize, std::move(future_2),
            std::ref(start_time), std::ref(current_idx));

        // We append the dummy futures into a double-ended queue.
        visualize_futures.push_back(std::move(future_vis));
        // And check that the pipeline is full. We do not want our pipeline to launch
        // dozens of threads ahead of time since we know that the visualization will
        // anyway run in 1 second intervals.
        if (visualize_futures.size() > pipeline_depth)
        {
            // At this point the main thread will call a destructor of the oldest future,
            // which in its turn will block until the thread gets joined.
            visualize_futures.pop_front();
        }

        std::cout << "Enqueued sample: " << idx << std::endl;
    }

    std::cout << "Waiting to finish..." << std::endl;
    // Here we gracefully stop the pipeline by waiting for all threads to join.
    for (auto& fut : visualize_futures)
    {
        fut.get();
    }

    // And we are done!
    std::cout << "Finished!" << std::endl;
    return 0;
}
