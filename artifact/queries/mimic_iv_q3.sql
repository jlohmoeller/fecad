-- Q3: Combined (4 attributes)
-- Elderly sepsis-AKI patients with elevated creatinine (>1.2 mg/dL -> q=120)
SELECT * FROM data WHERE icd_sepsis = 1 AND icd_aki = 1 AND age >= 65 AND creatinine_q >= 120
