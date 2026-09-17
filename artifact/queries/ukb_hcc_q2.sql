-- Q2: Disease condition (3 attributes)
-- Participants with HCC or major precursor conditions
SELECT * FROM data WHERE icd_hcc = 1 OR icd_cirrhosis = 1 OR icd_hepatitis = 1
