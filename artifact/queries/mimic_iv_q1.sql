-- Q1: Data availability (3 attributes)
-- ICU patients with complete age and lab data
SELECT * FROM data WHERE age >= 18 AND creatinine_q >= 0 AND lactate_q >= 0
