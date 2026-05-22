// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "PrintStats.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/Stats.h>
#include <omni/scene.optimizer/core/Utils.h>

// Carbonite
#include <carb/profiler/Profile.h>

// C++
#include <iomanip>
#include <iostream>
#include <locale>

PXR_NAMESPACE_USING_DIRECTIVE

SO_PLUGIN_INIT(omni::scene::optimizer::StatsOperation);


namespace omni::scene::optimizer
{


struct CommaFacet : public std::numpunct<char>
{
    char do_thousands_sep() const override
    {
        return ',';
    }

    std::string do_grouping() const override
    {
        return "\03";
    }
};


static std::string _getFormattedNumber(const size_t number)
{
    std::locale locale(std::cout.getloc(), new CommaFacet);

    std::stringstream ss;
    ss.imbue(locale);
    ss << number;

    return ss.str();
};


static std::string _getExtraFromPrimInfo(const PrimInfo& primInfo)
{
    std::vector<std::string> extraInfo;

    // Check for leaf xforms or unique materials - currently only populated for those types
    if (primInfo.leaf)
    {
        extraInfo.push_back(_getFormattedNumber(primInfo.leaf) + " leaf");
    }

    if (primInfo.disjoint)
    {
        extraInfo.push_back(_getFormattedNumber(primInfo.disjoint) + " disjoint");
    }

    // Unique count handled differently for meshes and materials.
    if (!primInfo.uniqueMaterials.empty())
    {
        extraInfo.push_back(_getFormattedNumber(primInfo.uniqueMaterials.size()) + " unique");
    }
    else if (primInfo.unique)
    {
        extraInfo.push_back(_getFormattedNumber(primInfo.unique) + " unique");
    }

    return TfStringJoin(extraInfo, ", ");
}


static std::string _getTotalFromPrimInfo(const PrimInfo& primInfo)
{
    std::string result = _getFormattedNumber(primInfo.count);

    std::string extra = _getExtraFromPrimInfo(primInfo);
    if (!extra.empty())
    {
        result += " (" + extra + ")";
    }

    return result;
}


static JsObject _getJsonFromPrimInfo(const PrimInfo& primInfo)
{
    JsObject result;
    result["count"] = JsValue(primInfo.count);
    result["leaf"] = JsValue(primInfo.leaf);
    result["disjoint"] = JsValue(primInfo.disjoint);
    result["inactive"] = JsValue(primInfo.inactive);
    result["invisible"] = JsValue(primInfo.invisible);

    if (!primInfo.uniqueMaterials.empty())
    {
        result["unique"] = JsValue(primInfo.uniqueMaterials.size());
    }
    else if (primInfo.unique)
    {
        result["unique"] = JsValue(primInfo.unique);
    }
    else
    {
        result["unique"] = JsValue(0);
    }

    result["extra"] = JsValue(_getExtraFromPrimInfo(primInfo));

    return result;
}


static JsObject _getJsonFromPrimvarStats(const PrimvarStats& primvarStats)
{
    JsObject result;

    result["count"] = JsValue(primvarStats.count);
    result["valueCount"] = JsValue(primvarStats.valueCount);
    result["size"] = JsValue(primvarStats.valueCount * primvarStats.sizeOf);

    return result;
}


/// Helper struct for formatting print output.
struct TextColumn
{
    explicit TextColumn(const std::string& _title)
        : title(_title)
    {
    }


    void addRow(const std::string& row)
    {
        width = 0;
        rows.push_back(row);
    }


    void addRow(const size_t value)
    {
        width = 0;
        rows.push_back(_getFormattedNumber(value));
    }


    std::string getRow(const size_t index) const
    {
        std::ostringstream oss;
        oss << std::left << std::setw(getWidth()) << rows[index];
        return oss.str();
    }


    int getWidth() const
    {
        if (!width)
        {
            width = static_cast<int>(title.length());
            for (const auto& row : rows)
            {
                width = std::max(width, static_cast<int>(row.length()));
            }
        }

        return width;
    }

    std::string title;
    std::vector<std::string> rows;
    mutable int width = 0;
};


class TextTable
{

public:
    explicit TextTable(std::vector<TextColumn>&& columns)
        : m_columns(std::move(columns))
    {
    }

    int getColumnWidths() const
    {
        int width = (static_cast<int>(m_columns.size()) - 1) * 2;
        for (const auto& column : m_columns)
        {
            width += column.getWidth();
        }

        return width;
    }


    void addEmptyRow()
    {
        for (size_t colIndex = 0; colIndex < m_columns.size(); ++colIndex)
        {
            m_columns[colIndex].addRow("");
        }
    }


    template <class... Ts>
    void addRow(Ts&&... values)
    {
        size_t i = 0;
        (m_columns[i++].addRow(values), ...);
    }


    void addRowToColumn(const size_t colIndex, const std::string& value)
    {
        m_columns[colIndex].addRow(value);
    }


    void addRowToColumn(const size_t colIndex, const size_t value)
    {
        m_columns[colIndex].addRow(_getFormattedNumber(value));
    }


    const std::vector<TextColumn>& columns() const
    {
        return m_columns;
    }

private:
    std::vector<TextColumn> m_columns;
};


static void _printTable(const TextTable& table, std::ostringstream& oss, const int widthTotal)
{
    const int colsWidth = table.getColumnWidths();
    const int remainder = widthTotal - colsWidth;

    std::ostringstream header;
    const auto& columns = table.columns();
    for (size_t i = 0; i < columns.size(); ++i)
    {
        header << std::left << std::setw(columns[i].getWidth()) << columns[i].title;
        if (i != columns.size() - 1)
        {
            header << "  ";
        }
    }

    oss << "| " << std::setw(widthTotal) << std::setfill(' ') << header.str() << " |" << std::endl;

    // Separator between headings/values
    oss << "|" << std::setw(widthTotal + 2) << std::setfill('=') << ""
        << "|" << std::endl;
    oss << std::setfill(' ');

    size_t rows = columns[0].rows.size();

    // Actual data
    for (size_t rowIndex = 0; rowIndex < rows; ++rowIndex)
    {
        oss << "| ";
        for (size_t colIndex = 0; colIndex < columns.size(); ++colIndex)
        {
            oss << columns[colIndex].getRow(rowIndex);
            if (colIndex != columns.size() - 1)
            {
                oss << "  ";
            }
        }

        // If this table is shorter than another, pad out.
        if (remainder)
        {
            oss << std::setw(remainder) << std::setfill(' ') << "";
        }

        oss << " |" << std::endl;
    }
}


StatsOperation::StatsOperation()
    : Operation("printStats", "Stats", "Collect and display statistics about the contents of a USD stage")
{
    addArgument("countPrimvars",
                "Count Primvars/Values",
                kDisplayTypeBool,
                "Count the number of primvars and their values",
                m_countPrimvars);

    addArgument("splitCollocatedPoints",
                "Split Collocated Points",
                kDisplayTypeBool,
                "Should points that are collocated be considered part of a disjoint mesh",
                m_splitCollocated);

    addArgument("time",
                "Time",
                kDisplayTypeFloat,
                "Collect stats at the specified time (default nan means UsdTimeCode::Default)",
                m_time);
}


std::string StatsOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion StatsOperation::getVersion() const
{
    return { 1, 0, 0 };
}


bool StatsOperation::getVisible() const
{
    return false;
}


std::string StatsOperation::getCategory() const
{
    return "OUTPUT";
}


bool StatsOperation::getSupportsAnalysis() const
{
    // This entire operation is kind of "analysis mode".
    return true;
}


static void _preparePayload(const StatCountersUPtr& stats, OperationResult& result)
{
    // Prim type counts
    JsObject primTypes;
    for (const auto& [primType, primInfo] : stats->primTypes)
    {
        primTypes[primType] = _getJsonFromPrimInfo(primInfo);
    }

    JsObject primvars;
    for (const auto& [primvarName, primvarStats] : stats->primvars)
    {
        primvars[primvarName] = _getJsonFromPrimvarStats(primvarStats);
    }

    // Assemble payload
    JsObject payload;
    payload["types"] = primTypes;
    payload["prims"] = JsValue(stats->prims);
    payload["primvars"] = primvars;
    payload["timeSamples"] = JsValue(stats->timeSamples);
    payload["instanceable"] = JsValue(stats->instanceable);
    payload["instances"] = JsValue(stats->instances);
    payload["inactive"] = JsValue(stats->inactive);
    payload["invisible"] = JsValue(stats->invisible);
    payload["vertices"] = JsValue(stats->vertices);

    JsObject analysis;
    analysis["analysis"] = payload;

    // Write as compact rather than "pretty" JSON
    std::ostringstream oss;
    JsWriter writer(oss, JsWriter::Style::Compact);
    JsWriteValue(&writer, analysis);

    result.output = getCStr(oss.str());
}


OperationResult StatsOperation::executeAnalysisImpl()
{
    // Stats operation is really just analysis in its own right, but to "support" analysis
    // mode this function exists and just calls the standard code path.
    return executeImpl();
}


OperationResult StatsOperation::executeImpl()
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|StatsOperation|Execute");

    // Configure args
    StatArgs args;
    args.setCountPrimvars(m_countPrimvars);
    args.setSplitCollocated(m_splitCollocated);

    // Set Time. Note: our default, quiet_NaN, is actually what UsdTimeCode uses
    // for ::Default() - so we can just wrap m_time
    args.setTimeCode(UsdTimeCode(m_time));

    // Legacy - we never had proper op args for these, they just ran off "verbose".
    if (getContext()->verbose)
    {
        args.setDisjoint(true);
        args.setTimeSamples(true);
    }

    // Get statistics
    StatCountersUPtr stats = _collectSceneStats(getUsdStage(), args);

    TextTable primTable({ TextColumn("Prim Type"), TextColumn("Count"), TextColumn("Inactive"), TextColumn("Invisible") });

    // Populate column values
    for (const auto& info : stats->primTypes)
    {
        // Add the first three columns
        primTable.addRow(info.first, _getTotalFromPrimInfo(info.second), info.second.inactive);

        // Special-case: materials/shaders are not imageable, add the final column manually
        if (info.first == "Shader" || info.first == "Material")
        {
            primTable.addRowToColumn(3, "--");
        }
        else
        {
            primTable.addRowToColumn(3, info.second.invisible);
        }
    }

    // Empty row to separate totals
    primTable.addEmptyRow();

    // Totals
    primTable.addRow("Total", stats->prims, stats->inactive, stats->invisible);

    // Primvars Table. May or may not get populated.
    TextTable primvarTable({ TextColumn("Primvar Name"), TextColumn("Count"), TextColumn("Values"), TextColumn("Size") });

    // If countPrimvars was enabled, populate the table
    if (m_countPrimvars)
    {
        size_t totalCount = 0;
        size_t totalValueCount = 0;
        size_t totalBytes = 0;

        // Populate column values
        for (const auto& it : stats->primvars)
        {
            long int bytes = static_cast<long int>(it.second.valueCount * it.second.sizeOf);
            std::string _bytes = _getFormattedBytes(static_cast<double>(bytes));
            primvarTable.addRow(it.first.GetString(), it.second.count, it.second.valueCount, _bytes);

            totalCount += it.second.count;
            totalValueCount += it.second.valueCount;
            totalBytes += bytes;
        }

        primvarTable.addEmptyRow();

        std::string _totalBytes = _getFormattedBytes(static_cast<double>(totalBytes));
        primvarTable.addRow("Total", totalCount, totalValueCount, _totalBytes);
    }

    // Main Heading
    std::string title = "Stage Stats:";

    // Default widths
    int widthTotal = static_cast<int>(title.length());

    // Work out total width. Size of each column, plus 2 spaces between each, depending
    // on which table will be wider.
    int colsWidth = primTable.getColumnWidths();

    // If including primvars, make sure we compensate for whichever table ends up
    // taking more horizontal space.
    if (m_countPrimvars)
    {
        colsWidth = std::max(colsWidth, primvarTable.getColumnWidths());
    }

    // Total internal width
    widthTotal = std::max(widthTotal, colsWidth);

    // clang-format off
    std::ostringstream oss;
    oss << "+" << std::setw(widthTotal + 2) << std::setfill('-') << "" << "+" << std::endl;
    oss << "| " << std::setfill(' ') << std::left << std::setw(widthTotal) << title << " |" << std::endl;
    oss << "| " << std::setw(widthTotal) << "" << " |" << std::endl;

    // Instancing info
    if (stats->instanceable || stats->instances || stats->prototypes)
    {
        std::string strInstanceable = "Instanceable: " + _getFormattedNumber(stats->instanceable);
        std::string strInstances = "Instances: " + _getFormattedNumber(stats->instances);
        std::string strPrototypes = "Instance prototypes: " + _getFormattedNumber(stats->prototypes);

        oss << "| " << std::setw(widthTotal) << strInstanceable << " |" << std::endl;
        oss << "| " << std::setw(widthTotal) << strInstances << " |" << std::endl;
        oss << "| " << std::setw(widthTotal) << strPrototypes << " |" << std::endl;

        oss << "| " << std::setw(widthTotal) << "" << " |" << std::endl;
    }

    // Geometric info
    if (stats->faces || stats->vertices)
    {
        if (stats->faces)
        {
            std::string strFaces = "Faces: " + _getFormattedNumber(stats->faces);
            oss << "| " << std::setw(widthTotal) << strFaces << " |" << std::endl;
        }

        if (stats->vertices)
        {
            std::string strVertices = "Vertices: " + _getFormattedNumber(stats->vertices);
            oss << "| " << std::setw(widthTotal) << strVertices << " |" << std::endl;
        }

        std::string strGeometries = "Renderable Geometries: " + _getFormattedNumber(stats->geometries);
        oss << "| " << std::setw(widthTotal) << strGeometries << " |" << std::endl;

        oss << "| " << std::setw(widthTotal) << "" << " |" << std::endl;
    }

    // Time samples
    if (stats->timeSamples)
    {
        std::string strTimeSamples = "TimeSamples: " + _getFormattedNumber(stats->timeSamples);
        oss << "| " << std::setw(widthTotal) << strTimeSamples << " |" << std::endl;

        oss << "| " << std::setw(widthTotal) << "" << " |" << std::endl;
    }

    // Prim Types
    _printTable(primTable, oss, widthTotal);

    // Primvars
    if (m_countPrimvars)
    {
        oss << "| " << std::setw(widthTotal) << "" << " |" << std::endl;
        _printTable(primvarTable, oss, widthTotal);
    }

    // Footer
    oss << "+" << std::setw(widthTotal + 2) << std::setfill('-') << "" << "+" << std::endl;
    // clang-format on

    // Generate the JSON output
    OperationResult result{ true, nullptr, nullptr };
    _preparePayload(stats, result);

    // If reporting is enabled, or analysis + verbose, log the payload.
    if ((result.output != nullptr && getContext()->generateReport) ||
        (getContext()->analysisMode && getContext()->verbose))
    {
        SO_LOG_INFO("PAYLOAD: %s", result.output);
    }
    else
    {
        // If not in reporting mode then just dump to stdout.
        std::cout << oss.str() << std::endl;
    }

    return result;
}


} // namespace omni::scene::optimizer
