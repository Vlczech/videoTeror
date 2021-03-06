#include "vtsense/Tracking/objecttracker.h"
#include "vtsense/Helpers/helpers.h"

using namespace VideoTeror::Tracking;

ObjectTracker::ObjectTracker(ObjectDetection::ObjectDetector &detector, const Settings &settings) :
    detector(detector),
    settings(settings),
    stopRequest(false)
{

}

void ObjectTracker::detectAndTrack(const VideoTeror::BGRImage &prevFrame,
                                   const VideoTeror::BGRImage &currentFrame,
                                   int frameIndex, Result &result)
{
    if (stopRequest)
    {
        stopRequest = false;
        throw ComputationCanceled("Computation canceled at frame " + std::to_string(frameIndex));
    }

    const cv::Size frameSize(prevFrame.cols, prevFrame.rows);

    VideoTeror::GrayscaleImage prevGSFrame, currentGSFrame;
    cv::cvtColor(prevFrame, prevGSFrame, CV_BGR2GRAY);
    cv::cvtColor(currentFrame, currentGSFrame, CV_BGR2GRAY);


    // Find objects in current frame
    VideoTeror::ObjectDetection::ObjectDetector::DetectionResult::vector currentFrameObjects = detector.detect(currentFrame);
    //std::cout << "frame " << frameIndex << "; detections: " << currentFrameObjects.size() << std::endl;

    // track points
    if (!trackingPoints.empty()) trackingPoints = pointTracker.track(prevGSFrame, currentGSFrame, trackingPoints);

    for (VideoTeror::ObjectDetection::ObjectDetector::DetectionResult o : currentFrameObjects)
    {
        // does the newly detected object belong to some of tracked points?
        int pointIndex = getTrackedPointIndex(o, frameSize);

        // yes, the object belong to some point
        if (pointIndex >= 0)
        {
            //std::cout<< "object already registered" << std::endl;

            o.id = pointIds[pointIndex];
            result.objectsPerFrame[frameIndex].push_back(o);
            result.trajectories[o.id].points.push_back(o.point);

            // alter the tracked point position based on the detection
            trackingPoints[pointIndex] = o.toPixelPoint(frameSize);
            missCounter[pointIndex] = 0;
        }
        else // no, it seems that the new object has appeared
        {
            //std::cout<< "new object appeared" << std::endl;
            int newId = result.trajectories.size();
            o.id = newId;
            result.trajectories[newId] = VideoTeror::Tracking::Trajectory(frameIndex, o.point);
            result.objectsPerFrame[frameIndex].push_back(o);

            cv::Point2f point = o.toPixelPoint(frameSize);
            trackingPoints.push_back(point);
            missCounter.push_back(0);
            pointIds.push_back(newId);
        }
    }

    std::vector<int> toRemove;
    for (int pIndex = 0; pIndex < trackingPoints.size(); pIndex++)
    {
        // Check if point is in some rectangle
        bool hit = false;
        for (const VideoTeror::ObjectDetection::ObjectDetector::DetectionResult &o : currentFrameObjects)
        {
            cv::Rect rect = o.toPixelRegion(frameSize);
            if (rect.contains(trackingPoints[pIndex]))
            {
                // But at the same time, the point has to be the nearest to the rectangle center
                cv::Point rectCenter = o.toPixelPoint(frameSize);
                double dist = VideoTeror::Helpers::Helpers::euclDist(trackingPoints[pIndex], rectCenter);
                double minDist = 1e300;
                for (int i = 0; i < trackingPoints.size(); i++)
                {
                    if (i == pIndex) continue;
                    double d = VideoTeror::Helpers::Helpers::euclDist(trackingPoints[i], rectCenter);
                    if (d < minDist) minDist = d;
                }

                if (dist < minDist)
                {
                    hit = true;
                    break;
                }
            }
        }

        if (!hit)
        {
            //std::cout<< "no object detected, but i have something from previous frame" << std::endl;
            missCounter[pIndex]++;

            int id = pointIds[pIndex];
            VideoTeror::ObjectDetection::ObjectDetector::DetectionResult object =
                    VideoTeror::ObjectDetection::ObjectDetector::DetectionResult::findResult(result.objectsPerFrame[frameIndex-1], id);

            double dist = VideoTeror::Helpers::Helpers::euclDist(object.toPixelPoint(frameSize), trackingPoints[pIndex]);
            if (dist < settings.maxMoveThreshold)
            {
                auto p = cv::Point2d(((double)trackingPoints[pIndex].x) / currentFrame.cols,
                                     ((double)trackingPoints[pIndex].y) / currentFrame.rows);

                object.region.x = object.region.x + (p.x - object.point.x);
                object.region.y = object.region.y + (p.y - object.point.y);
                object.point = p;
            }

            result.trajectories[id].points.push_back(object.point);
            result.objectsPerFrame[frameIndex].push_back(object);
        }

        // is some point outside of rectangle for long time?
        if (missCounter[pIndex] >= settings.forgetThreshold) toRemove.push_back(pIndex);
    }

    // remove points without object
    cleanUpPoints(toRemove);
}

ObjectTracker::Result ObjectTracker::detectAndTrack(cv::VideoCapture &source, std::function<void (int)> progress)
{
    int len = source.get(CV_CAP_PROP_FRAME_COUNT);
    int frameCounter = 1;
    ObjectTracker::Result result;

    VideoTeror::BGRImage frame, prev;
    source.read(prev);

    while(source.read(frame))
    {
        detectAndTrack(prev, frame, frameCounter, result);
        frame.copyTo(prev);

        if (progress)
        {
            int percent = frameCounter * 100 / len;
            progress(percent);
        }

        frameCounter++;
    }

    return result;
}

void ObjectTracker::cleanUpPoints(const std::vector<int> &toRemove)
{
    VideoTeror::Points newPoints;
    std::vector<int> newMissCounter;
    std::vector<int> newPointIds;

    for (int i = 0; i < trackingPoints.size(); i++)
    {
        if (std::find(toRemove.begin(), toRemove.end(), i) == toRemove.end())
        {
            newPoints.push_back(trackingPoints[i]);
            newMissCounter.push_back(missCounter[i]);
            newPointIds.push_back(pointIds[i]);
        }
    }

    trackingPoints = newPoints;
    missCounter = newMissCounter;
    pointIds = newPointIds;
}

int ObjectTracker::getTrackedPointIndex(const ObjectDetection::ObjectDetector::DetectionResult &detectedObject, const cv::Size &frameSize)
{
    int minIndex = -1;
    double minDistance = 1e300;
    int n = trackingPoints.size();
    for (int i = 0; i < n; i++)
    {
        bool hit = detectedObject.toPixelRegion(frameSize).contains(cv::Point(trackingPoints[i].x, trackingPoints[i].y));
        if (hit)
        {
            double dist = VideoTeror::Helpers::Helpers::euclDist(detectedObject.toPixelPoint(frameSize), trackingPoints[i]);
            if (dist < minDistance)
            {
                minDistance = dist;
                minIndex = i;
            }
        }
    }

    return minIndex;
}

VideoTeror::GrayscaleImage ObjectTracker::Result::drawTrajectories(const cv::Size &size) const
{
    VideoTeror::GrayscaleImage result = VideoTeror::GrayscaleImage::ones(size)*255;
    for (const std::pair<int, VideoTeror::Tracking::Trajectory> &t : trajectories)
    {
        for (int i = 1; i < t.second.points.size(); i++)
        {
            cv::Point prev(t.second.points[i-1].x * size.width, t.second.points[i-1].y * size.height);
            cv::Point next(t.second.points[i].x * size.width, t.second.points[i].y * size.height);
            cv::line(result, prev, next, 0);
        }
    }

    return result;
}
