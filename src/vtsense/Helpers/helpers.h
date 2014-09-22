#ifndef VT_HELPERS_H
#define VT_HELPERS_H

#include "Tracking/tracker.h"

class QStringList;
class QDir;

namespace VideoTeror
{
namespace Helpers
{

class Helpers
{
public:
    static VideoTeror::Points merge(VideoTeror::Points &in, float mergeDistanceThreshold);

    static inline float euclDist(cv::Point2f &p1, cv::Point2f &p2)
    {
        return sqrt((p1.x-p2.x)*(p1.x-p2.x) + (p1.y-p2.y)*(p1.y-p2.y));
    }

    static cv::Rect crop(const cv::Rect &in, int maxX, int maxY);

    static void drawKeyPoints(cv::Mat &img, std::vector<cv::KeyPoint> &keypoints);

    static QStringList recursiveSearch(const QDir &dir, const QStringList &filters);
};

}
}

#endif // VT_HELPERS_H