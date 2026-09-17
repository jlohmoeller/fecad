#include "pdserver/data/DatabaseWrapper.hpp"

#include <ranges>

#include "pdserver/config/CommandLineParameters.hpp"

namespace pat_disc {
    DatabaseWrapper *DatabaseWrapper::s_Instance = new DatabaseWrapper;

    int DatabaseWrapper::Initialize()
    {
        PD_TRACE("Initializing database wrapper...");

        const std::filesystem::path p = server::CommandLineParameters::GetInstance().GetDatabaseFile();
        create_directories(p.parent_path());

        int rc = sqlite3_open_v2(p.c_str(), &m_Database, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);

        if (rc)
        {
            PD_ERROR("Could not open database file {}. Error: {}", p.string(), sqlite3_errmsg(m_Database));

            return 1;
        }

        if (server::CommandLineParameters::GetInstance().IsDropTablesOnStart())
        {
            DropAllTables();
        }

        CreateAllCiphertextTables();
        CreatePatientDataTable();

        for (const AttributeType type: AttributeTypes())
        {
            m_CurrentIds.insert({type, GetNextInsertId(type)});
        }

        return 0;
    }

    void DatabaseWrapper::Close() const
    {
        sqlite3_close_v2(m_Database);
    }

    void DatabaseWrapper::StartTransaction() const
    {
        int rc = sqlite3_exec(m_Database, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
        PD_ASSERT(rc == SQLITE_OK, "Failed to begin database transaction");
    }

    void DatabaseWrapper::CommitTransaction() const
    {
        int rc = sqlite3_exec(m_Database, "COMMIT TRANSACTION;", nullptr, nullptr, nullptr);
        PD_ASSERT(rc == SQLITE_OK, "Failed to commit database transaction");
    }

    void DatabaseWrapper::InitializeAttributeIdMap(std::unordered_map<std::string, int64_t> attributeIdMap) const
    {
        for (const auto& [key, attr]: MatchableAttribute::GetAttributes())
        {
            attributeIdMap.insert({key, m_CurrentIds.at(attr->GetAttributeType())});
        }
    }

    void DatabaseWrapper::UpdateNextIdInfo(const int64_t insertPatientCount)
    {
        for (AttributeType type: AttributeTypes())
        {
            if (MatchableAttribute::GetAttributeCountForType(type) > 0)
            {
                uint32_t batchSize = MatchableAttribute::GetBatchSizeForType(type);

                if (insertPatientCount % batchSize == 0)
                {
                    m_CurrentIds[type] += insertPatientCount / batchSize;
                } else
                {
                    m_CurrentIds[type] += insertPatientCount / batchSize + 1;
                }
            }
        }
    }

    sqlite3_stmt *DatabaseWrapper::StartRetrieveCiphertextsOfAttributeType(AttributeType type, std::vector<std::string> &attrs, int32_t &resultCount) const
    {
        for (const auto &[key, value]: MatchableAttribute::GetAttributes())
        {
            if (value->GetAttributeType() == type)
            {
                attrs.push_back(key);
            }
        }

        if (attrs.empty())
            return nullptr;

        {
            std::string sql = std::format("SELECT COUNT(ID) FROM {};", attrs.front());
            sqlite3_stmt *stmt = CreateStatement(sql);
            const int rc = sqlite3_step(stmt);

            if (rc == SQLITE_ROW)
                resultCount = sqlite3_column_int(stmt, 0);
            else
                resultCount = 0;

            FinalizeStatement(stmt);
        }

        {
            std::stringstream ss;
            ss << std::format("SELECT {0}.ID as \"ID\", {0}.Data, ", attrs.front());
            for (auto it = attrs.begin() + 1; it != attrs.end(); ++it)
            {
                ss << std::format("{}.Data, ", *it);
            }
            ss.seekp(-2, std::stringstream::cur);

            ss << std::format(" FROM {}", attrs.front());

            for (auto it = attrs.begin() + 1; it != attrs.end(); ++it)
            {
                ss << std::format(" JOIN {0} ON {1}.ID = {0}.ID", *it, attrs.front());
            }
            ss << ";";

            return CreateStatement(ss.str());
        }
    }

    int64_t DatabaseWrapper::FetchNextCiphertext(sqlite3_stmt *stmt, const std::vector<std::string> &attributes, std::map<std::string, std::string> &data, int32_t &rc)
    {
        rc = sqlite3_step(stmt);

        if (rc == SQLITE_ROW)
        {
            const int64_t ctId = sqlite3_column_int64(stmt, 0);

            int32_t columnIndex = 1;
            for (const auto &attr: attributes)
            {
                const void *blob = sqlite3_column_blob(stmt, columnIndex);
                const int size = sqlite3_column_bytes(stmt, columnIndex);

                std::string d(static_cast<const char *>(blob), size);
                data.emplace(attr, std::move(d));
                
                columnIndex++;
            }

            return ctId;
        }

        return -1;
    }

    void DatabaseWrapper::RetrievePatientData(const std::set<AttributeType> &types, const std::function<void(const PatientDataTable &data)> &onData) const
    {
        std::stringstream ss;
        ss << "SELECT ID, Provider, ";

        for (auto type: AttributeTypes())
        {
            if (!types.contains(type))
                continue;

            ss << std::format("{0}Id, {0}Index, ", kAttributeTypeNames.at(type));
        }
        ss.seekp(-2, std::stringstream::cur);

        ss << " FROM PatientData;";
        auto stmt = CreateStatement(ss.str());

        int rc;
        do
        {
            rc = sqlite3_step(stmt);

            if (rc == SQLITE_ROW)
            {
                std::map<AttributeType, std::pair<int64_t, int64_t> > ctInfo;

                std::string id = GetString(stmt, 0);
                std::string provider = GetString(stmt, 1);

                int32_t columnIndex = 2;
                for (const auto &type: AttributeTypes())
                {
                    if (!types.contains(type))
                        continue;

                    const int idType = sqlite3_column_type(stmt, columnIndex);
                    const int indexType = sqlite3_column_type(stmt, columnIndex + 1);

                    if (idType == SQLITE_NULL || indexType == SQLITE_NULL)
                        PD_ASSERT(false, "Requested attribute type is not stored in database")

                    int64_t ctId = sqlite3_column_int64(stmt, columnIndex);
                    int64_t ctIndex = sqlite3_column_int64(stmt, columnIndex + 1);

                    ctInfo.insert({type, std::pair{ctId, ctIndex}});
                    columnIndex += 2;
                }

                onData({id, provider, ctInfo});
            }
        } while (rc == SQLITE_ROW);

        if (rc != SQLITE_DONE)
        {
            PD_ERROR("Error retrieving patient data. Error: {}", sqlite3_errmsg(m_Database));
        }

        FinalizeStatement(stmt);
    }

    void DatabaseWrapper::CreateAllCiphertextTables() const
    {
        for (const auto &[key, _]: MatchableAttribute::GetAttributes())
        {
            CreateCiphertextsTable(key);
        }
    }

    void DatabaseWrapper::CreateCiphertextsTable(const std::string &attr) const
    {
        std::string formatted = std::format("CREATE TABLE IF NOT EXISTS {}(ID INTEGER PRIMARY KEY NOT NULL, Data BLOB NOT NULL);", attr);

        int rc = sqlite3_exec(m_Database, formatted.c_str(), nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK)
        {
            PD_ERROR("Could not execute statement {}. Error: {}", formatted, sqlite3_errmsg(m_Database));
        }
    }

    void DatabaseWrapper::CreatePatientDataTable() const
    {
        std::stringstream ss;
        ss << "CREATE TABLE IF NOT EXISTS PatientData(";
        ss << "ID TEXT NOT NULL, Provider TEXT NOT NULL, ";

        for (auto type: AttributeTypes())
        {
            ss << std::format("{0}Id INTEGER, {0}Index INTEGER, ", kAttributeTypeNames.at(type));
        }

        ss << "PRIMARY KEY (ID, Provider)); ";

        const int rc = sqlite3_exec(m_Database, ss.str().c_str(), nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK)
        {
            PD_ERROR("Could not execute statement {}. Error: {}", ss.str(), sqlite3_errmsg(m_Database));
        }
    }

    void DatabaseWrapper::DropAllTables() const
    {
        std::stringstream ss;
        ss << "DROP TABLE IF EXISTS PatientData;";

        for (auto &key: std::views::keys(MatchableAttribute::GetAttributes()))
        {
            ss << std::format("DROP TABLE IF EXISTS {};", key);
        }

        int rc = sqlite3_exec(m_Database, ss.str().c_str(), nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK)
        {
            PD_WARN("Failed to drop tables. Error: {}", sqlite3_errmsg(m_Database));
        }
    }

    sqlite3_stmt *DatabaseWrapper::StartInsertCiphertext(const std::string &attr) const
    {
        const std::string insertCiphertext = std::format("INSERT INTO {}(ID, Data) VALUES (?, ?);", attr);
        return CreateStatement(insertCiphertext);
    }

    void DatabaseWrapper::ResetStatement(sqlite3_stmt *stmt) const
    {
        int rc = sqlite3_reset(stmt);
        if (rc != SQLITE_OK)
        {
            PD_ERROR("Could not reset statement. Error: {}", sqlite3_errmsg(m_Database));
        }
    }

    void DatabaseWrapper::FinalizeStatement(sqlite3_stmt *stmt) const
    {
        int rc = sqlite3_finalize(stmt);
        if (rc != SQLITE_OK)
        {
            PD_ERROR("Could not finalize statement. Error: {}", sqlite3_errmsg(m_Database));
        }
    }

    sqlite3_stmt *DatabaseWrapper::StartInsertPatientData() const
    {
        std::stringstream ss;
        ss << "INSERT INTO PatientData(ID, Provider, ";

        for (auto type: AttributeTypes())
        {
            ss << std::format("{0}Id, {0}Index, ", kAttributeTypeNames.at(type));
        }

        ss.seekp(-2, std::stringstream::cur);
        ss << ") VALUES (?, ?, ";

        for ([[maybe_unused]] auto _: AttributeTypes())
        {
            ss << "?, ?, ";
        }

        ss.seekp(-2, std::stringstream::cur);
        ss << ");";

        return CreateStatement(ss.str());
    }

    sqlite3_stmt *DatabaseWrapper::CreateStatement(const std::string &sql) const
    {
        sqlite3_stmt *stmt;
        int rc = sqlite3_prepare_v2(m_Database, sql.c_str(), (int) sql.size(), &stmt, nullptr);

        if (rc != SQLITE_OK)
        {
            PD_ERROR("Could not prepare statement {}. Error: {}", sql, sqlite3_errmsg(m_Database));
        }

        return stmt;
    }

    void DatabaseWrapper::InsertCiphertext(sqlite3_stmt *stmt, const int64_t id, const std::string &data) const
    {
        int rc = sqlite3_bind_int64(stmt, 1, id);

        if (rc != SQLITE_OK)
        {
            PD_ERROR("Could not bind int64 to statement. Error: {}", sqlite3_errmsg(m_Database));
        }

        rc = sqlite3_bind_blob(stmt, 2, data.c_str(), static_cast<int>(data.size()), SQLITE_STATIC);

        if (rc != SQLITE_OK)
        {
            PD_ERROR("Could not bind blob to statement. Error: {}", sqlite3_errmsg(m_Database));
        }

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE)
        {
            PD_ERROR("Could not insert ciphertext data. Error: {}", sqlite3_errmsg(m_Database));
        }
    }

    int64_t DatabaseWrapper::InsertPatientData(sqlite3_stmt *stmt, const std::string &provider, const std::string &id, const int64_t offset) const
    {
        int rc = sqlite3_bind_text(stmt, 1, id.c_str(), static_cast<int>(id.size()), SQLITE_STATIC);
        if (rc != SQLITE_OK)
        {
            PD_ERROR("Could not bind text to statement. Error: {}", sqlite3_errmsg(m_Database));
        }

        rc = sqlite3_bind_text(stmt, 2, provider.c_str(), static_cast<int>(provider.size()), SQLITE_STATIC);
        if (rc != SQLITE_OK)
        {
            PD_ERROR("Could not bind text to statement. Error: {}", sqlite3_errmsg(m_Database));
        }

        int32_t currentColIndex = 3;
        for (AttributeType type: AttributeTypes())
        {
            const uint32_t batchSize = MatchableAttribute::GetBatchSizeForType(type);

            if (MatchableAttribute::GetAttributeCountForType(type) > 0)
            {
                BindIdAndIndex(stmt, currentColIndex, m_CurrentIds.at(type) + offset / batchSize, offset % batchSize);
            } else
            {
                BindIdAndIndex(stmt, currentColIndex, -1, -1);
            }

            currentColIndex += 2;
        }

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE)
        {
            PD_ERROR("Could not insert patient data. Error: {}", sqlite3_errmsg(m_Database));
        }

        return sqlite3_last_insert_rowid(m_Database);
    }

    void DatabaseWrapper::BindIdAndIndex(sqlite3_stmt *stmt, const int colIndex, const int64_t id, const int64_t index) const
    {
        int rc;
        if (id >= 0)
        {
            rc = sqlite3_bind_int64(stmt, colIndex, id);
            if (rc != SQLITE_OK)
            {
                PD_ERROR("Could not bind int to statement. Error: {}", sqlite3_errmsg(m_Database));
            }
        } else
        {
            rc = sqlite3_bind_null(stmt, colIndex);
            if (rc != SQLITE_OK)
            {
                PD_ERROR("Could not bind int to statement. Error: {}", sqlite3_errmsg(m_Database));
            }
        }

        if (index >= 0)
        {
            rc = sqlite3_bind_int64(stmt, colIndex + 1, index);
            if (rc != SQLITE_OK)
            {
                PD_ERROR("Could not bind int to statement. Error: {}", sqlite3_errmsg(m_Database));
            }
        } else
        {
            rc = sqlite3_bind_null(stmt, colIndex + 1);
            if (rc != SQLITE_OK)
            {
                PD_ERROR("Could not bind int to statement. Error: {}", sqlite3_errmsg(m_Database));
            }
        }
    }

    std::string DatabaseWrapper::GetString(sqlite3_stmt *stmt, int colIndex)
    {
        const unsigned char *ptr = sqlite3_column_text(stmt, colIndex);
        const int32_t size = sqlite3_column_bytes(stmt, colIndex);

        std::string result;
        result.resize(size);
        memcpy(result.data(), ptr, size);

        return result;
    }

    int64_t DatabaseWrapper::GetNextInsertId(const AttributeType type) const
    {
        const auto sql = std::format("SELECT {}Id FROM PatientData ORDER BY {}Id DESC LIMIT 1;", kAttributeTypeNames.at(type), kAttributeTypeNames.at(type));
        const auto stmt = CreateStatement(sql);

        int64_t result = -1;
        if (const int rc = sqlite3_step(stmt); rc == SQLITE_ROW)
        {
            result = sqlite3_column_int64(stmt, 0);
        }
        FinalizeStatement(stmt);

        return result + 1;
    }
}
