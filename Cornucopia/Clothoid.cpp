/*--
    Clothoid.cpp  

    This file is part of the Cornucopia curve sketching library.
    Copyright (C) 2010 Ilya Baran (ibaran@mit.edu)

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "Clothoid.h"
#include "Fresnel.h"

using namespace std;
using namespace Eigen;
NAMESPACE_Cornu

Clothoid::Clothoid(const Vec &start, double startAngle, double length, double curvature, double endCurvature)
{
    _params.resize(numParams());

    _params.head<2>() = start;
    _params[ANGLE] = AngleUtils::toRange(startAngle);
    _params[LENGTH] = length;
    _params[CURVATURE] = curvature;
    _params[DCURVATURE] = (endCurvature - curvature) / length;

    _paramsChanged();
}

void Clothoid::_paramsChanged()
{
    Vector2d startcs;

    _arc = fabs(_params[DCURVATURE]) < 1e-12;
    _flat = false;

    if(_arc)
    {
        _flat = fabs(_params[CURVATURE]) < 1e-6;

        if(_flat)
        {
            _t1 = 0;
            _tdiff = 1;

            double angle = _params[ANGLE];
            double cosA = cos(angle), sinA = sin(angle);

            _mat << cosA, -sinA,
                    sinA, cosA;

            startcs = Vector2d(0, 0);
        }
        else //non-flat arc
        {
            _t1 = 0;
            _tdiff = _params[CURVATURE];

            double angle = _params[ANGLE] - HALFPI;
            double cosAS = cos(angle) / _params[CURVATURE], sinAS = sin(angle) / _params[CURVATURE];

            _mat << cosAS, -sinAS,
                    sinAS, cosAS;

            startcs = Vector2d(1, 0);
        }
    }
    else //clothoid
    {
        double scale = sqrt(fabs(1. / (PI * _params[DCURVATURE])));

        _t1 = _params[CURVATURE] * scale;
        _tdiff = _params[DCURVATURE] * scale;

        if(_tdiff > 0)
        {
            double angleShift = _params[ANGLE] - _t1 * _t1 * HALFPI;
            double cosAS = PI * scale * cos(angleShift), sinAS = PI * scale * sin(angleShift);
            _mat << cosAS, -sinAS,
                    sinAS, cosAS;
        }
        else //we need a reflection here
        {
            double angleShift = _params[ANGLE] + _t1 * _t1 * HALFPI;
            double cosAS = PI * scale * cos(angleShift), sinAS = PI * scale * sin(angleShift);
            _mat << -cosAS, -sinAS,
                    -sinAS, cosAS;
        }

        fresnel(_t1, &(startcs[1]), &(startcs[0]));
    }

    _startShift = _startPos() - _mat * startcs;    
}

bool Clothoid::isValid() const
{
    if(_params[LENGTH] < 0.)
        return false;
    return true;
}

void Clothoid::eval(double s, Vec *pos, Vec *der, Vec *der2) const
{
    if(pos)
    {
        double t = _t1 + s * _tdiff;

        Vector2d cs;
        if(_flat)
            cs = Vector2d(t, 0);
        else if(_arc)
            cs = Vector2d(cos(t), sin(t));
        else
            fresnel(t, &(cs[1]), &(cs[0]));

        (*pos) = _startShift + _mat * cs;
    }
    if(der || der2)
    {
        double angle = _params[ANGLE] + s * (_params[CURVATURE] + 0.5 * s * _params[DCURVATURE]);
        double cosa = cos(angle), sina = sin(angle);

        if(der)
            (*der) = Vector2d(cosa, sina);
        if(der2)
            (*der2) = (_params[CURVATURE] + s * _params[DCURVATURE]) * Vector2d(-sina, cosa);
    }
}

double Clothoid::angle(double s) const
{
    return _params[ANGLE] + s * (_params[CURVATURE] + 0.5 * s * _params[DCURVATURE]);
}

double Clothoid::curvature(double s) const
{
    return _params[CURVATURE] + s * _params[DCURVATURE];
}

double Clothoid::project(const Vec &point) const
{
    //TODO
    return 0;
}

void Clothoid::trim(double sFrom, double sTo)
{
    Vec newStart = pos(sFrom);
    _params[ANGLE] = angle(sFrom); //this line must come before the curvature change
    _params[CURVATURE] = curvature(sFrom); //the angle change in the previous line doesn't affect curvature
    _params[LENGTH] = sTo - sFrom;
    _params[X] = newStart[0];
    _params[Y] = newStart[1];

    _paramsChanged();
}

void Clothoid::flip()
{
    Vec newStart = endPos();
    _params[ANGLE] = PI + endAngle();
    _params[CURVATURE] = -endCurvature();
    _params[X] = newStart[0];
    _params[Y] = newStart[1];

    _paramsChanged();
}

void Clothoid::derivativeAt(double s, ParamDer &out)
{
    out = ParamDer::Zero(6, 2);
    out(X, 0) = 1;
    out(Y, 1) = 1;

    Vec diff = pos(s) - _startPos();
    out(ANGLE, 0) = -diff[1];
    out(ANGLE, 1) = diff[0];

    //Now compute derivatives with respect to curvature and the curvature
    //derivative.  These were derived with Mathematica and limits.
    if(_flat)
    {
        Vec nsinCos = Vec(-sin(_params[ANGLE]), cos(_params[ANGLE]));
        out.row(CURVATURE) = (0.5 * s * s) * nsinCos;
        out.row(DCURVATURE) = (0.5 / 3. * s * s * s) * nsinCos;
    }
    else if(_arc)
    {
        double curv = _params[CURVATURE];
        double curAngle = angle(s);
        double cosCur = cos(curAngle), sinCur = sin(curAngle);
        double cosStart = cos(_params[ANGLE]), sinStart = sin(_params[ANGLE]);
        double curvs = curv * s;
        Vec curvDer(curvs * cosCur + sinStart - sinCur, curvs * sinCur + cosCur - cosStart);
        out.row(CURVATURE) = curvDer / (curv * curv);
        Vec dcurvDer(cosStart + (curvs * curvs * 0.5 - 1.) * cosCur - curvs * sinCur,
                          sinStart + (curvs * curvs * 0.5 - 1.) * sinCur + curvs * cosCur);
        out.row(DCURVATURE) = dcurvDer / (curv * curv * curv);
    }
    else //non-degenerate
    {
        //This block is half-derived from Mathematica, and half from the considerations below.
        //Unfortunately it's a mess, but it works (see CurveDerivativesTest).
        //Any change to what we need to compute will require rederivation.
        //There's a bunch of optimizations possible, but they'll make the code even worse.
        //
        //dt/dx = dt1/dx + s * dtdiff/dx
        //dcs/dx = cossin(pi t^2 / 2) * dt/dx
        //dstartcs/dx = cossin(pi t1^2 / 2) * dt1/dx
        //dp/dx = dmat/dx * (cs - startcs) + mat * (dcs/dx - dstartcs/dx)
        
        double t = _t1 + s * _tdiff;
        double scale = sqrt(fabs(1. / (PI * _params[DCURVATURE])));
        RowVector2d dt1dx(scale, -_params[CURVATURE] * scale / (2. * _params[DCURVATURE]));
        RowVector2d dtdx = dt1dx + RowVector2d(0, scale * s * 0.5);
        Vector2d cs, startcs;
        fresnel(t, &(cs[1]), &(cs[0]));
        fresnel(_t1, &(startcs[1]), &(startcs[0]));
        double basicAngle = HALFPI * _t1 * _t1;
        Vector2d dstartcs(cos(basicAngle), sin(basicAngle));
        Vector2d dcs(cos(HALFPI * t * t), sin(HALFPI * t * t));

        Matrix2d result = (_mat * dcs) * dtdx - (_mat * dstartcs) * dt1dx;
        Matrix2d dmatdc, dmatdd;

        double angleShift;
        if(_tdiff > 0.)
            angleShift = _params[ANGLE] - _t1 * _t1 * HALFPI;
        else
            angleShift = _params[ANGLE] + _t1 * _t1 * HALFPI;

        double cosAS = cos(angleShift), sinAS = sin(angleShift);
        dmatdc << sinAS, cosAS,
                  -cosAS, sinAS;
        dmatdc *= PI * scale * _params[CURVATURE] / _params[DCURVATURE];
        double curvSqr = _params[CURVATURE] * _params[CURVATURE];
        dmatdd(0, 0) = -_params[DCURVATURE] * cosAS - curvSqr * sinAS;
        dmatdd(1, 1) = dmatdd(0, 0);
        dmatdd(0, 1) = _params[DCURVATURE] * sinAS - curvSqr * cosAS;
        dmatdd(1, 0) = -dmatdd(0, 1);
        dmatdd *= HALFPI * scale / (_params[DCURVATURE] * _params[DCURVATURE]);

        if(_tdiff < 0.)
        {
            dmatdc.col(0) *= -1;
            dmatdd.col(0) *= -1;
        }

        result.col(0) += dmatdc * (cs - startcs);
        result.col(1) += dmatdd * (cs - startcs);

        out.block<2, 2>(CURVATURE, 0) = result.transpose();
    }
}

END_NAMESPACE_Cornu


