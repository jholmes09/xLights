#ifndef CIRCLEMODEL_H
#define CIRCLEMODEL_H

#include "Model.h"


class CircleModel : public Model
{
    public:
        CircleModel(wxXmlNode *node, const NetInfoClass &netInfo, bool zeroBased = false);
        virtual ~CircleModel();

        virtual int GetStrandLength(int strand) const override;
        virtual int MapToNodeIndex(int strand, int node) const override;
        virtual int GetNumStrands() const override;
        
    protected:
        virtual void InitModel() override;
        
    private:
        void SetCircleCoord();
        void InitCircle();

        std::vector<int> circleSizes;
};

#endif // CIRCLEMODEL_H
