-- Q1: Data availability (3 attributes)
-- Patients with complete spatial, molecular, and demographic data
SELECT * FROM data WHERE idhWildType >= 0 AND age >= 0 AND tumorPos_x >= 0
