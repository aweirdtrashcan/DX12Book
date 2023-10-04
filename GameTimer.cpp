//
// Created by Diego on 04/10/2023.
//

#include "GameTimer.h"

#include <Windows.h>
#include "DXRenderer.h"

#define mGetCurrTime(currTime) (QueryPerformanceCounter((LARGE_INTEGER*)&currTime))

GameTimer::GameTimer()
{
    __int64 countsPerSecond;
    QueryPerformanceFrequency((LARGE_INTEGER*)&countsPerSecond);

    mSecondsPerCount = 1.0 / (double)countsPerSecond;
}

void GameTimer::Reset()
{
    if (mBaseTime != 0)
    {
        DXRenderer::Log("[WARNING]: Calling GameTimer::Reset() more than once.\n");
    }

    __int64 currTime;
    mGetCurrTime(currTime);

    mBaseTime = currTime;
    mPrevTime = currTime;
    mStopTime = 0;
    mStopped = false;
}

void GameTimer::Start()
{
    if (mStopped)
    {
        __int64 startTime;
        mGetCurrTime(startTime);

        mPausedTime += startTime - mStopTime;
        mStopTime = 0;
        mPrevTime = startTime;
        mStopped = false;
    }
    else
    {
        DXRenderer::Log("[WARNING]: GameTimer is not paused for you to call GameTimer::Start()\n");
    }
}

void GameTimer::Stop()
{
    if (!mStopped)
    {
        mGetCurrTime(mStopTime);
        mStopped = true;
    }
    else
    {
        DXRenderer::Log("[WARNING]: GameTimer is not running for you to call GameTimer::Stop()\n");
    }
}

void GameTimer::Tick()
{
    if (mStopped)
    {
        mDeltaTime = 0.0;
        return;
    }

    // Get the time this frame.
    mGetCurrTime(mCurrTime);

    // Time difference between this frame and the previous
    mDeltaTime = (mCurrTime - mPrevTime) * mSecondsPerCount;

    // Prepare for next frame.
    mPrevTime = mCurrTime;

    /* Force nonnegative. The DXSDK’s CDXUTTimer
        mentions that if the
        processor goes into a power save mode or we get
        shuffled to
        another processor, then mDeltaTime can be
        negative.
    */
    if (mDeltaTime < 0.0)
        mDeltaTime = 0.0;

}