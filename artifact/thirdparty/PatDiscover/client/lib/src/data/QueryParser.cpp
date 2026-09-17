//
// Created by Niels Pressel on 17.10.2025.
//

#include "pdclient/data/QueryParser.hpp"

#include <antlr4-runtime.h>
#include "SQLLexer.h"
#include "SQLParser.h"

#include "SQLParserBaseVisitor.h"

namespace pat_disc {
    class QueryParserVisitor : public SQLParserBaseVisitor
    {
    public:
        QueryParserVisitor() = default;

        QueryBuilder&& GetBuilder()
        {
            return std::move(m_Builder);
        }

    private:
        std::any visitElement(SQLParser::ElementContext *context) override
        {
            antlr4::tree::ParseTree *node = context->children[0];
            if (context->bool_expr() || context->enum_expr() || context->range_expr() || context->distance_expr())
            {
                auto data = std::any_cast<QueryAttributeData>(node->accept(this));
                CombineGroup *cg = m_Builder.AddCombineGroup(data.GetAttributeType(), CombineOperator::SUM);
                cg->MoveAttributeData(std::move(data));

                return {};
            }

            auto group = std::any_cast<CombineGroup>(node->accept(this));
            m_Builder.MoveCombineGroup(group.GetAttributeType(), std::move(group));

            return {};
        }

        std::any visitBool_paren_group(SQLParser::Bool_paren_groupContext *context) override
        {
            return context->bool_group_expr()->accept(this);
        }

        std::any visitBool_group_expr(SQLParser::Bool_group_exprContext *context) override
        {
            return context->children[0]->accept(this);
        }

        std::any visitBool_and_group(SQLParser::Bool_and_groupContext *context) override
        {
            if (context->bool_expr(0))
                return ProcessGroupedTerminal(context, CombineOperator::AND);

            return ProcessGrouped(context, CombineOperator::AND);
        }

        std::any visitBool_or_group(SQLParser::Bool_or_groupContext *context) override
        {
            if (context->bool_expr(0))
                return ProcessGroupedTerminal(context, CombineOperator::OR);

            return ProcessGrouped(context, CombineOperator::OR);
        }

        std::any visitBool_sum_group(SQLParser::Bool_sum_groupContext *context) override
        {
            if (context->bool_expr(0))
                return ProcessGroupedTerminal(context, CombineOperator::SUM);

            return ProcessGrouped(context, CombineOperator::SUM);
        }

        std::any visitEnum_paren_group(SQLParser::Enum_paren_groupContext *context) override
        {
            return context->enum_group_expr()->accept(this);
        }

        std::any visitEnum_group_expr(SQLParser::Enum_group_exprContext *context) override
        {
            return context->children[0]->accept(this);
        }

        std::any visitEnum_and_group(SQLParser::Enum_and_groupContext *context) override
        {
            if (context->enum_expr(0))
                return ProcessGroupedTerminal(context, CombineOperator::AND);

            return ProcessGrouped(context, CombineOperator::AND);
        }

        std::any visitEnum_or_group(SQLParser::Enum_or_groupContext *context) override
        {
            if (context->enum_expr(0))
                return ProcessGroupedTerminal(context, CombineOperator::OR);

            return ProcessGrouped(context, CombineOperator::OR);
        }

        std::any visitEnum_sum_group(SQLParser::Enum_sum_groupContext *context) override
        {
            if (context->enum_expr(0))
                return ProcessGroupedTerminal(context, CombineOperator::SUM);

            return ProcessGrouped(context, CombineOperator::SUM);
        }

        std::any visitRange_paren_group(SQLParser::Range_paren_groupContext *context) override
        {
            return context->range_group_expr()->accept(this);
        }

        std::any visitRange_group_expr(SQLParser::Range_group_exprContext *context) override
        {
            return context->children[0]->accept(this);
        }

        std::any visitRange_and_group(SQLParser::Range_and_groupContext *context) override
        {
            if (context->range_expr(0))
                return ProcessGroupedTerminal(context, CombineOperator::AND);

            return ProcessGrouped(context, CombineOperator::AND);
        }

        std::any visitRange_or_group(SQLParser::Range_or_groupContext *context) override
        {
            if (context->range_expr(0))
                return ProcessGroupedTerminal(context, CombineOperator::OR);

            return ProcessGrouped(context, CombineOperator::OR);
        }

        std::any visitRange_sum_group(SQLParser::Range_sum_groupContext *context) override
        {
            if (context->range_expr(0))
                return ProcessGroupedTerminal(context, CombineOperator::SUM);

            return ProcessGrouped(context, CombineOperator::SUM);
        }

        std::any visitDistance_and_group(SQLParser::Distance_and_groupContext *context) override
        {
            if (context->distance_expr(0))
                return ProcessGroupedTerminal(context, CombineOperator::AND);

            return ProcessGrouped(context, CombineOperator::AND);
        }

        std::any visitDistance_or_group(SQLParser::Distance_or_groupContext *context) override
        {
            if (context->distance_expr(0))
                return ProcessGroupedTerminal(context, CombineOperator::OR);

            return ProcessGrouped(context, CombineOperator::OR);
        }

        std::any visitDistance_sum_group(SQLParser::Distance_sum_groupContext *context) override
        {
            if (context->distance_expr(0))
                return ProcessGroupedTerminal(context, CombineOperator::SUM);

            return ProcessGrouped(context, CombineOperator::SUM);
        }

        std::any visitDistance_paren_group(SQLParser::Distance_paren_groupContext *context) override
        {
            return context->distance_group_expr()->accept(this);
        }

        std::any visitDistance_group_expr(SQLParser::Distance_group_exprContext *context) override
        {
            return context->children[0]->accept(this);
        }

        std::any visitBool_expr(SQLParser::Bool_exprContext *context) override
        {
            const std::string attributeName = context->ID()->getText();
            QueryAttributeData attributeData(attributeName, false);
            attributeData.SetBooleanValue(context->TRUE() ? 1 : 0);

            return attributeData;
        }

        std::any visitEnum_expr(SQLParser::Enum_exprContext *context) override
        {
            const std::string attributeName = context->ID()->getText();
            const AttributeType type = MatchableAttribute::GetAttributeType(attributeName);
            PD_ASSERT(type == AttributeType::ENUM_PRECISE || type == AttributeType::ENUM_APPROX, "Invalid attribute");

            QueryAttributeData attributeData(attributeName, context->NEQ() != nullptr);
            const int64_t value = std::stoll(context->INT()->getText());
            if (type == AttributeType::ENUM_PRECISE)
                attributeData.SetPreciseEnumValue(value);
            else
                attributeData.SetApproxEnumValue(value);

            return attributeData;
        }

        std::any visitRange_expr(SQLParser::Range_exprContext *context) override
        {
            const std::string attributeName = context->ID()->getText();
            const AttributeType type = MatchableAttribute::GetAttributeType(attributeName);
            PD_ASSERT(type == AttributeType::CONTINUOUS_PRECISE || type == AttributeType::CONTINUOUS_APPROX, "Invalid attribute");

            QueryAttributeData attributeData(attributeName, context->NOT() != nullptr);
            const double lower = std::stod(context->INT(0) ? context->INT(0)->getText() : context->REAL(0)->getText());
            const double upper = std::stod(context->INT(1) ? context->INT(1)->getText() : context->REAL(1)->getText());

            if (type == AttributeType::CONTINUOUS_PRECISE)
                attributeData.SetPreciseContinuousValue(lower, upper);
            else
                attributeData.SetApproxContinuousValue(lower, upper);

            return attributeData;
        }

        std::any visitDistance_expr(SQLParser::Distance_exprContext *context) override
        {
            const std::string attributeName = context->ID()->getText();
            const AttributeType type = MatchableAttribute::GetAttributeType(attributeName);
            PD_ASSERT(type == AttributeType::DISTANCE_PRECISE || type == AttributeType::DISTANCE_APPROX, "Invalid attribute");

            QueryAttributeData attributeData(attributeName, context->GT() != nullptr);
            SQLParser::PointContext *point = context->point();
            const double x = std::stod(point->REAL(0)->getText());
            const double y = std::stod(point->REAL(1)->getText());
            const double z = std::stod(point->REAL(2)->getText());
            const double bound = std::stod(context->REAL()->getText());

            if (type == AttributeType::DISTANCE_PRECISE)
                attributeData.SetPreciseDistanceValue(x, y, z, bound);
            else
                attributeData.SetApproxDistanceValue(x, y, z, bound);

            return attributeData;
        }

    private:
        CombineGroup ProcessGroupedTerminal(antlr4::tree::ParseTree *node, CombineOperator combineOperator)
        {
            CombineGroup cg(combineOperator);
            for (const auto child : node->children)
            {
                const auto data = child->accept(this);
                if (data.type() == typeid(QueryAttributeData))
                {
                    auto attr = std::any_cast<QueryAttributeData>(data);
                    cg.MoveAttributeData(std::move(attr));
                }
            }

            return cg;
        }

        CombineGroup ProcessGrouped(antlr4::tree::ParseTree *node, CombineOperator combineOperator)
        {
            CombineGroup cg(combineOperator);
            for (const auto child : node->children)
            {
                const auto data = child->accept(this);
                if (data.type() == typeid(CombineGroup))
                {
                    auto group = std::any_cast<CombineGroup>(data);
                    cg.MoveCombineGroup(std::move(group));
                }
            }

            return cg;
        }

    private:
        QueryBuilder m_Builder;
    };

    QueryBuilder QueryParser::ParseQueryString(const std::string &q)
    {
        antlr4::ANTLRInputStream input(q);
        SQLLexer lexer(&input);
        antlr4::CommonTokenStream tokenStream(&lexer);
        SQLParser parser(&tokenStream);

        SQLParser::QueryContext *queryContext = parser.query();

        QueryParserVisitor visitor;
        visitor.visit(queryContext);

        return visitor.GetBuilder();
    }
}

