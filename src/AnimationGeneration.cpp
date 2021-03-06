// AnimationGeneration.cpp - backend animation generation functions
// Author: Sarah
// Date last updated: 05/22/2016
//
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdio.h>
#include <sstream>
#include <string.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include "modeldata.pb.h"
#include "point.h"
#include "hermite.cpp"
#include "diff_hermite.h"
#include "AnimationLayering.cpp"

using namespace swellanimations;
using namespace std;

/* returns the length of the spline by adding the distances between all interpolated points */
/* retthis is the length of the user drawn curve */
double getSplineLength(vector<struct pt*> spline) {
    double length = 0;
    for (int i = 0; i < spline.size()-1; i++) {
        double d = getDistance(spline.at(i), spline.at(i+1));
        length += fabs(d);
    }
    return length;
}

/* returns the length of the longest path from the root */
/* this is roughly the length of the model in the z direction */
double furthestNodeDistance(Node root) {
    double max = (double)fabs(root.mutable_position()->z());
    for (int i = 0; i < root.children_size(); i++) {
        double n = furthestNodeDistance(root.children(i));
        if (n > max)
            max = n;
    }
    return max;
}

double getDistanceFromModelVectors(Vector* p1, Vector* p2) {
	float diffY = p1->y() - p2->y();
	float diffX = p1->x() - p2->x();
	float diffZ = p1->z() - p2->z();
	return sqrt((diffY * diffY) + (diffX * diffX) + (diffZ * diffZ));
}

/* returns the length of the model by adding the distances between joints */
double getModelLength(Node* model) {
	double length = 0;
	while (model->children_size() > 0) {
		//We are assuming only 1 child per node.  Currently that is all we send.
		Node* child = model->mutable_children(0);
		length += getDistanceFromModelVectors(model->mutable_position(), child->mutable_position());
		model = child;
	}
	return length;
}

/* returns the indices in the spline that correspond to joints of the model in the first frame */
/* maps the points in the spline to the joints in the model based on the length of the model 
    and how many points in the spline will make up a single frame */
vector<int> mapPoints(Node* root, int maxIndexOnStartFrame, double modelLength) {
    vector<int> total;
	float distanceRatioFromFront = getModelLength(root) / modelLength;
	int correspondingIndex = round(distanceRatioFromFront * maxIndexOnStartFrame);
	total.push_back(correspondingIndex);
    for (int i = 0; i < root->children_size(); i++) {
		vector<int> c = mapPoints(root->mutable_children(i), maxIndexOnStartFrame, modelLength);
        for (int j = 0; j < c.size(); j++) {
            total.push_back(c.at(j));
        }
    }
    return total;
}

/* returns a new tree (frame) with new positions based on the calculated corresponding points in the spline */
/* when called in succession, it moves the model and all of its joints along the spline */
Node jointsToSpline(Node* root, vector<struct pt*> spline, vector<int> correspondingPoints, int &index, ofstream *myfile) {
	Node frame;
	frame.set_name(root->name());
	//*myfile << frame.name() << endl;

	int c = correspondingPoints.at(index);
	struct pt* s = spline.at(c);

	double x0, x1, x2;
	double y0, y1, y2;
	double z0, z1, z2;

	struct pt* s0;
	struct pt* s2;
	struct pt* m0;

	if (c == 0)
	{
		s2 = spline.at(c + 1);
		m0 = forwardDiff(s, s2);
	}
	else if (c == spline.size() - 1)
	{
		s0 = spline.at(c - 1);
		m0 = forwardDiff(s0, s);
	}
	else
	{
		s0 = spline.at(c - 1);
		s2 = spline.at(c + 1);
		m0 = midpointDiff(s0, s, s2);
	}

	frame.mutable_position()->set_x(s->x);
	frame.mutable_position()->set_y(s->y);
	frame.mutable_position()->set_z(s->z);

	frame.mutable_eularangles()->set_x(m0->x);
	frame.mutable_eularangles()->set_y(m0->y);
	frame.mutable_eularangles()->set_z(m0->z);

	//*myfile << "-- ";
	//*myfile << root.name() << endl;
	//*myfile << frame.mutable_position()->z() << endl;

	for (int i = 0; i < root->children_size(); i++) {
		Node tmp = jointsToSpline(root->mutable_children(i), spline, correspondingPoints, ++index, myfile);
		tmp.set_name(root->children(i).name());
		Node* p = frame.add_children();
		p->CopyFrom(tmp);
	}
	return frame;
}

/* uses hermite.cpp to calculate a spline based on control points */
/* for rolling and keyframing (connecting separated LOAs) */
std::vector<struct pt*> getSpline(ModelData* modelData) {
    std::vector<struct pt*> v;
    // TODO: check for self intersection of line
    // idea: check if any two control points have the exact same position

    // get indices of mutable_controlpoints for each of the 4 new control points
	float frameIncs = modelData->controlpoints_size() / (float) modelData->numberofframes();
	float currentInc = frameIncs;
    // if line contains no self intersections
    // get user drawn curve from frontend and store in a vector
    //
    //
    for (int i = 0; i < modelData->controlpoints_size()-1; i++) {
        Vector* p0Vec = modelData->mutable_controlpoints(i);
        Vector* p1Vec = modelData->mutable_controlpoints(i+1);

        struct pt* p0 = createPoint(p0Vec->x(), p0Vec->y(), p0Vec->z()); 
        struct pt* p1 = createPoint(p1Vec->x(), p1Vec->y(), p1Vec->z()); 
        struct pt* m0;
        struct pt* m1;

        if(i == 0)
        {
            Vector* p2Vec = modelData->mutable_controlpoints(i+2);
            struct pt* p2 = createPoint(p2Vec->x(), p2Vec->y(), p2Vec->z()); 
            m0 = forwardDiff(p0, p1);
            m1 = midpointDiff(p0, p1, p2);
        }
        else if(i == modelData->controlpoints_size()-2)
        {
            Vector* p2Vec = modelData->mutable_controlpoints(i-2);
            struct pt* p2 = createPoint(p2Vec->x(), p2Vec->y(), p2Vec->z()); 
            m1 = forwardDiff(p0, p1);
            m0 = midpointDiff(p2, p0, p1);
        }
        else
        {
            Vector* p00Vec = modelData->mutable_controlpoints(i-1);
            struct pt* p00 = createPoint(p00Vec->x(), p00Vec->y(), p00Vec->z()); 
            Vector* p2Vec = modelData->mutable_controlpoints(i+2);
            struct pt* p2 = createPoint(p2Vec->x(), p2Vec->y(), p2Vec->z()); 
            m0 = midpointDiff(p00, p0, p1);
            m1 = midpointDiff(p0, p1, p2);
        }

		if (currentInc < i + 1){
			for (double t = (currentInc - i + 1); t < 1; t += frameIncs) {
				struct pt* r = hermite(t, p0, m0, p1, m1);
				v.push_back(r);
				currentInc += frameIncs;
			}
		}
    }

    return v;
}


/* returns an animation of the model evaluated at a certain point along spline */
// TODO: get the time it takes the user to draw the LOA, going to need the control points dropped at intervals
Animation* evaluateDLOA(ModelData* modelData, vector<struct pt*> spline) {
    Animation* animation = new Animation();
    ofstream myfile;
    //myfile.open ("/home/psarahdactyl/Documents/ccfunfunfun.txt");

    // calculate the constant b
	double modelLength = getModelLength(modelData->mutable_model());
	double splineLength = getSplineLength(spline);
	double b = modelLength / (splineLength);

    // calculate points in spline per frame
    double pointsPerFrame = spline.size() * b;
    //myfile << pointsPerFrame << endl;

    // calculate which point goes with which joint
    Node* root = modelData->mutable_model();

    // maps points from user drawn curve -- now a spline -- to the joints in the model
	vector<int> correspondingPoints = mapPoints(root, pointsPerFrame, modelLength);
    for(int h = 0; h < correspondingPoints.size(); h++)
    {
        myfile << correspondingPoints.at(h) << endl;
    }

    vector<struct pt*> extra;

    struct pt* last = spline.at(spline.size()-1);
    struct pt* secondLast = spline.at(spline.size()-2);

    double x = last->x - secondLast->x;
    double y = last->y - secondLast->y;
    double z = last->z - secondLast->z;
    struct pt* difference = createPoint(x, y, z);

    for(double t = 1; t <= pointsPerFrame; t++)
    {
        struct pt* diff = multScalar(t, difference);
        struct pt* r = add(last, diff);
        extra.push_back(r);
    }

    vector<struct pt*> newSpline;
    newSpline.reserve( spline.size() + extra.size() ); // preallocate memory
    newSpline.insert( newSpline.end(), spline.begin(), spline.end() );
    newSpline.insert( newSpline.end(), extra.begin(), extra.end() );

    // go through every point in spline
    // iterating by 1 every time gives us frames overlapping points in the spline
    for (int i = 0; i < newSpline.size() - pointsPerFrame; i++) {
        int index = 0;
        // move model and its joints
        Node frame = jointsToSpline(root, newSpline, correspondingPoints, index,  &myfile);
	    frame.set_name(root->name());

        // go through the mapped joints on the model and move them up by 1
        // since we are on a new frame
        vector<int> newCorresponding;
        for (int j = 0; j < correspondingPoints.size(); j++) {
            newCorresponding.push_back(correspondingPoints.at(j)+1);
        }
        // copy for the next iteration 
        correspondingPoints = newCorresponding;

        // add frames to the animation
        Node* a = animation->add_frames();
        a->CopyFrom(frame);
	    a->set_name(root->name());
    }

    return animation;
}

void copySplineToAnimation(vector<struct pt*> spline, Animation* animation){
	for (int x = 0; x < spline.size(); x++){
		struct pt* p = spline.at(x);
		Vector* vector = animation->add_spline();
		vector->set_x(p->x);
		vector->set_y(p->y);
		vector->set_z(p->z);
	}
}

/* May need someone to double check that I'm using ModelData appropriately since I've never messed around with it before */
void applyRotationPoints(ModelData* modelData, Animation* animation) {
	Node model = modelData->model();

	// Store the rotation angle values in a vector for later use
	vector<struct pt*> rotationAngles;
	struct pt *point = new struct pt;
	RotationPoint rp;

	// Iterate over each rotation point and calculate the rotation angle for adjustment use in the x, y, and z dimensions
	for (int i = 0; i < modelData->rotationpoints_size(); i++) {
		rp = modelData->rotationpoints(i);

		struct pt *point = new struct pt;

		// The model will begin to rotate some specified number of frames before the rotation point- this number is retrieved in the numframes() method
		point->x = rp.mutable_rotation()->x();
		point->y = rp.mutable_rotation()->y();
		point->z = rp.mutable_rotation()->z();
		rotationAngles.push_back(point);
	}

	int startframe;
	int endframe;

	// Iterate over each rotation point, calculating the start frame and end frame
	for (int i = 0; i < modelData->rotationpoints_size(); i++) {
		rp = modelData->rotationpoints(i);
		startframe = rp.startframe();
		endframe = rp.startframe() + rp.numframes();

		animation->mutable_frames(endframe)->mutable_rotation()->set_x(rotationAngles.at(i)->x);
		animation->mutable_frames(endframe)->mutable_rotation()->set_y(rotationAngles.at(i)->y);
		animation->mutable_frames(endframe)->mutable_rotation()->set_z(rotationAngles.at(i)->z);
	}
}


float CalculateDistance(struct  pt* p1, Vector* p2) {
	float diffY = p1->y - p2->y();
	float diffX = p1->x - p2->x();
	float diffZ = p1->z - p2->z();
	return sqrt((diffY * diffY) + (diffX * diffX) + (diffZ * diffZ));
}


void adjustRotationPointLocations(ModelData* modelData, vector<struct pt*> spline) {
	for (int x = 0; x < modelData->rotationpoints_size(); x++) {
		RotationPoint* rotPoint = modelData->mutable_rotationpoints(x);
		int startFrame = rotPoint->startframe();
		int numFrames = rotPoint->numframes();
		Vector* location = modelData->mutable_controlpoints(startFrame + numFrames);
		float closestDistance = CalculateDistance(spline.at(0), location);
		int closestFrame = 0;
		for (int i = 1; i < spline.size(); i++){
			float distance = CalculateDistance(spline.at(i), location);
			if (distance < closestDistance) {
				closestDistance = distance;
				closestFrame = i;
			}
		}
		rotPoint->set_numframes(min(numFrames, closestFrame + 1));
		rotPoint->set_startframe(max(closestFrame - numFrames, 0));
	}
}

void adjustAnimationLayers(ModelData* modelData, vector<struct pt*> spline) {
	for (int x = 0; x < modelData->animationlayers_size(); x++) {
		AnimationLayer* animLayer = modelData->mutable_animationlayers(x);
		int startFrame = animLayer->startframe();
		int numFrames = animLayer->numframes();
		Vector* location = modelData->mutable_controlpoints(startFrame + numFrames);
		float closestDistance = CalculateDistance(spline.at(0), location);
		int closestFrame = 0;
		for (int i = 1; i < spline.size(); i++){
			float distance = CalculateDistance(spline.at(i), location);
			if (distance < closestDistance) {
				closestDistance = distance;
				closestFrame = i;
			}
		}
		animLayer->set_numframes(min(numFrames, closestFrame + 1));
		animLayer->set_startframe(max(closestFrame - numFrames, 0));
	}
}

/* returns an Animation object to send back to Unity */
Animation* getFrames(ModelData* modelData) {
	Node model = modelData->model();
	Animation* animation = new Animation();
    // get hermite spline
    vector<struct pt*> spline = getSpline(modelData);
    // evaluateDLOA
    animation = evaluateDLOA(modelData, spline);
	// apply rotation points to the model data
	adjustRotationPointLocations(modelData, spline);
	applyRotationPoints(modelData, animation);

	if(modelData->animationlayers_size() > 0) {
		//Call layering
		adjustAnimationLayers(modelData, spline);
		AddLayering(modelData, animation);
	}
	//set the spline in the return animation
	copySplineToAnimation(spline, animation);
	return animation;
}
