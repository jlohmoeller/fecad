-- Q1: Data availability (5 attributes)
-- Participants with complete demographic and liver function lab data
SELECT * FROM data WHERE age >= 0 AND sex >= 0 AND bmi_q >= 0 AND alt_q >= 0 AND ast_q >= 0
