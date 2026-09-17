#ifndef SERVER_DATABASEWRAPPER_HPP
#define SERVER_DATABASEWRAPPER_HPP

#include <pdproto/data_transfer_objects.pb.h>

namespace pat_disc {
    struct PatientDataTable
    {
        std::string id;
        std::string provider;
        std::map<AttributeType, std::pair<int64_t, int64_t> > ctInfo;
    };

    class DatabaseWrapper
    {
    public:
        static DatabaseWrapper &GetInstance()
        {
            return *s_Instance;
        };

        int Initialize();

        void Close() const;

        void StartTransaction() const;

        void CommitTransaction() const;

        // Insert management
        void InitializeAttributeIdMap(std::unordered_map<std::string, int64_t> attributeIdMap) const;

        void UpdateNextIdInfo(int64_t insertPatientCount);

        // Insert
        [[nodiscard]] sqlite3_stmt *StartInsertCiphertext(const std::string &attr) const;

        [[nodiscard]] sqlite3_stmt *StartInsertPatientData() const;

        int64_t InsertPatientData(sqlite3_stmt *stmt, const std::string &provider, const std::string &id, int64_t offset) const;

        void InsertCiphertext(sqlite3_stmt *stmt, int64_t id, const std::string &data) const;

        // Retrieve
        [[nodiscard]] sqlite3_stmt *StartRetrieveCiphertextsOfAttributeType(AttributeType type, std::vector<std::string> &attrs, int32_t &resultCount) const;

        static int64_t FetchNextCiphertext(sqlite3_stmt *stmt, const std::vector<std::string> &attributes, std::map<std::string, std::string> &data, int32_t &rc);

        void RetrievePatientData(const std::set<AttributeType> &types, const std::function<void(const PatientDataTable &data)> &onData) const;

        // Reset
        void ResetStatement(sqlite3_stmt *stmt) const;

        // Finalize
        void FinalizeStatement(sqlite3_stmt *stmt) const;

    private:
        // Create
        void CreateAllCiphertextTables() const;

        void CreateCiphertextsTable(const std::string &attr) const;

        void CreatePatientDataTable() const;

        // Drop
        void DropAllTables() const;

        // General
        [[nodiscard]] sqlite3_stmt *CreateStatement(const std::string &sql) const;

        // Retrieve
        void BindIdAndIndex(sqlite3_stmt *stmt, int colIndex, int64_t id, int64_t index) const;

        static std::string GetString(sqlite3_stmt *stmt, int colIndex);

        [[nodiscard]] int64_t GetNextInsertId(AttributeType type) const;

    private:
        static DatabaseWrapper *s_Instance;

        sqlite3 *m_Database = nullptr;
        std::map<AttributeType, int64_t> m_CurrentIds;
    };
}


#endif //SERVER_DATABASEWRAPPER_HPP
