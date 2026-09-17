-- Q_dist: Distance-based query (6 attributes)
-- Tumors within a 2 cm cube centred on reference point (2.0, 1.5, 1.0) m
-- Encoded: pos x10, so 2.0m -> 20, radius 2cm -> 0.2 units -> range [18,22] x [13,17] x [8,12]
SELECT * FROM data WHERE tumorPos_x >= 18 AND tumorPos_x <= 22 AND tumorPos_y >= 13 AND tumorPos_y <= 17 AND tumorPos_z >= 8 AND tumorPos_z <= 12
