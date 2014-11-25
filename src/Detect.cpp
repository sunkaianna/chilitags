/*******************************************************************************
 *   Copyright 2013-2014 EPFL                                                   *
 *   Copyright 2013-2014 Quentin Bonnard                                        *
 *                                                                              *
 *   This file is part of chilitags.                                            *
 *                                                                              *
 *   Chilitags is free software: you can redistribute it and/or modify          *
 *   it under the terms of the Lesser GNU General Public License as             *
 *   published by the Free Software Foundation, either version 3 of the         *
 *   License, or (at your option) any later version.                            *
 *                                                                              *
 *   Chilitags is distributed in the hope that it will be useful,               *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 *   GNU Lesser General Public License for more details.                        *
 *                                                                              *
 *   You should have received a copy of the GNU Lesser General Public License   *
 *   along with Chilitags.  If not, see <http://www.gnu.org/licenses/>.         *
 *******************************************************************************/

#include "Detect.hpp"

#include<iostream>

namespace chilitags{

#ifdef OPENCV3
#include <opencv2/core/utility.hpp>
#endif

Detect::Detect() :
    mRefineCorners(true),
    mFindQuads(),
    mRefine(),
    mReadBits(),
    mDecode(),
    mFrame(),
    mTags()
#ifdef WITH_PTHREADS
    ,mBackgroundThread(),
    mBackgroundRunning(false),
    mNeedFrame(true),
    mInputCond(PTHREAD_COND_INITIALIZER),
    mInputLock(PTHREAD_MUTEX_INITIALIZER),
    mLatestAsyncIdleMillis(0),
    mLatestAsyncWorkMillis(0)
#endif
{
}

void Detect::setMinInputWidth(int minWidth)
{
    mFindQuads.setMinInputWidth(minWidth);
}

void Detect::setCornerRefinement(bool refineCorners)
{
    mRefineCorners = refineCorners;
}

void Detect::doDetection(TagCornerMap& tags)
{
    if(mRefineCorners){
        for(const auto& quad : mFindQuads(mFrame)){
            auto refinedQuad = mRefine(mFrame, quad, 1.5f/10.0f);
            auto tag = mDecode(mReadBits(mFrame, refinedQuad), refinedQuad);
            if(tag.first != Decode::INVALID_TAG)
                tags[tag.first] = tag.second;
            else{
                tag = mDecode(mReadBits(mFrame, quad), quad);
                if(tag.first != Decode::INVALID_TAG)
                    tags[tag.first] = tag.second;
            }
        }
    }
    else{
        for(const auto& quad : mFindQuads(mFrame)){
            auto tag = mDecode(mReadBits(mFrame, quad), quad);
            if(tag.first != Decode::INVALID_TAG)
                tags[tag.first] = tag.second;
        }
    }
}

void Detect::operator()(cv::Mat const& greyscaleImage, TagCornerMap& tags)
{
#ifdef WITH_PTHREADS
    //Run single threaded
    if(!mBackgroundRunning){
        mFrame = greyscaleImage;
        doDetection(tags);
    }

    //Detection thread running in the background, just deliver the frame and tags
    else{
        if(mNeedFrame){
            pthread_mutex_lock(&mInputLock);

            greyscaleImage.copyTo(mFrame);
            mTags.clear();

            //Wake up detection thread if it's waiting for the input frame
            pthread_cond_signal(&mInputCond);
            pthread_mutex_unlock(&mInputLock);
        }
    }
#else
    mFrame = greyscaleImage;
    doDetection(tags);
#endif
}

#ifdef WITH_PTHREADS
void Detect::launchBackgroundThread(Track& track)
{
    if(!mBackgroundRunning){
        mTrack = &track;
        mBackgroundShouldRun = true;
        mBackgroundRunning = true;
        if(pthread_create(&mBackgroundThread, NULL, dispatchRun, (void*)this)){
            mBackgroundShouldRun = false;
            mBackgroundRunning = false;
            std::cerr << "Error: Thread could not be launched in " << __PRETTY_FUNCTION__
                << ", not enough resources or PTHREAD_THREADS_MAX was hit!" << std::endl;
        }
    }
}

void* Detect::dispatchRun(void* args)
{
    static_cast<Detect*>(args)->run();
    return NULL;
}

void Detect::run()
{
    while(mBackgroundShouldRun){
        pthread_mutex_lock(&mInputLock);

        //Wait for the input frame to arrive
        int64 startTime = cv::getTickCount();
        pthread_cond_wait(&mInputCond, &mInputLock); //This releases the lock while waiting
        mLatestAsyncIdleMillis = 1000.0f*(((float)cv::getTickCount() - (float)startTime)/cv::getTickFrequency());

        mNeedFrame = false;

        startTime = cv::getTickCount();
        doDetection(mTags);
        mTrack->update(mTags);
        mLatestAsyncWorkMillis = 1000.0f*(((float)cv::getTickCount() - (float)startTime)/cv::getTickFrequency());

        mNeedFrame = true;

        pthread_mutex_unlock(&mInputLock);
    }
    mBackgroundRunning = false;
}

float Detect::getLatestAsyncIdleMillis()
{
    return mLatestAsyncIdleMillis;
}

float Detect::getLatestAsyncWorkMillis()
{
    return mLatestAsyncWorkMillis;
}
#endif

} /* namespace chilitags */
