// SPDX-License-Identifier: AGPL-3.0-or-later
#include "obs/JuceRuntime.h"

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <obs-module.h>

#include <atomic>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

namespace stagemic::runtime
{

namespace
{
std::atomic<bool> running{false};

std::mutex queueMutex;
std::deque<std::function<void()>> queue;

// Runs everything queued so far
void drainQueue()
{
    for (;;)
    {
        std::deque<std::function<void()>> batch;
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            batch.swap(queue);
        }

        if (batch.empty())
            return;

        for (auto& fn : batch)
        {
            try
            {
                fn();
            }
            catch (const std::exception& e)
            {
                blog(LOG_ERROR, "[StageMic] Exception on UI thread: %s", e.what());
            }
            catch (...)
            {
                blog(LOG_ERROR, "[StageMic] Unknown exception on UI thread");
            }
        }
    }
}

#if JUCE_LINUX || JUCE_BSD
//------------------------------------------------------------------------------
// Dedicated JUCE message thread
std::thread messageThread;

bool startPlatform()
{
    std::promise<bool> ready;
    auto readyFuture = ready.get_future();

    messageThread = std::thread([&ready] {
        juce::Thread::setCurrentThreadName("StageMic UI");
        auto juceInit = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
        auto* mm = juce::MessageManager::getInstance();
        mm->setCurrentThreadAsMessageThread();
        ready.set_value(true);

        mm->runDispatchLoop(); // returns after stopDispatchLoop

        drainQueue();
        juceInit.reset();
    });

    return readyFuture.get();
}

void stopPlatform()
{
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
    {
        juce::MessageManager::callAsync([] { drainQueue(); });
        mm->stopDispatchLoop();
    }

    if (messageThread.joinable())
        messageThread.join();
}

#else
//------------------------------------------------------------------------------
// Windows
std::unique_ptr<juce::ScopedJuceInitialiser_GUI> juceInit;

bool startPlatform()
{
    juceInit = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
    juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread();
    return true;
}

void stopPlatform()
{
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    if (mm != nullptr && !mm->isThisTheMessageThread())
        blog(LOG_WARNING, "[StageMic] JUCE shutdown called off the UI thread");

    drainQueue();
    juceInit.reset();
}
#endif
} // namespace

bool start()
{
    if (running.load())
        return true;

    if (!startPlatform())
        return false;

    running = true;
    return true;
}

void stop()
{
    if (!running.exchange(false))
        return;

    stopPlatform();

    std::lock_guard<std::mutex> lock(queueMutex);
    if (!queue.empty())
        blog(LOG_WARNING, "[StageMic] %d UI task(s) dropped at shutdown", (int)queue.size());
    queue.clear();
}

bool isRunning() noexcept
{
    return running.load();
}

void post(std::function<void()> fn)
{
    if (!fn)
        return;

    if (!running.load())
    {
        blog(LOG_WARNING, "[StageMic] UI task posted while JUCE is not running; ignored");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        queue.push_back(std::move(fn));
    }

    juce::MessageManager::callAsync([] { drainQueue(); });
}

} // namespace stagemic::runtime
