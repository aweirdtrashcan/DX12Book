#pragma once
class GameTimer
{
public:
	GameTimer();

	/* In seconds */
	float TotalTime() const
	{
		if (mStopped)
		{
			return (float)(((mStopTime - mPausedTime) - mBaseTime) * mSecondsPerCount);
		}
		else
		{
			return (float)(((mCurrTime - mPausedTime) - mBaseTime) * mSecondsPerCount);
		}
	}
	__forceinline float DeltaTime() const { return (float)mDeltaTime; }

	void Reset(); // Call before message loop.
	void Start(); // Call when unpaused.
	void Stop(); // Call when paused.
	void Tick(); // Call every frame.

private:
	double mSecondsPerCount = 0.0;
	double mDeltaTime = 0.0;

	__int64 mBaseTime = 0;
	__int64 mPausedTime = 0;
	__int64 mStopTime = 0;
	__int64 mPrevTime = 0;
	__int64 mCurrTime = 0;

	bool mStopped = false;
};

