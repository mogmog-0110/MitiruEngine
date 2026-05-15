#pragma once
// Detail header for mitiru::Screen — do not include directly; included via core/Screen.hpp

inline void mitiru::Screen::pushTransform(float tx, float ty, float sx, float sy)
{
	const auto cur = currentTransform();
	// Compose: new = cur * translate(tx,ty) * scale(sx,sy)
	auto next = cur
	          * Transform2D::translate(tx, ty)
	          * Transform2D::scale(sx, sy);
	m_transformStack.push(next);
}

inline void mitiru::Screen::pushTransform(const Transform2D& t)
{
	m_transformStack.push(currentTransform() * t);
}

inline void mitiru::Screen::pushRotation(float rad, float pivotX, float pivotY)
{
	pushTransform(Transform2D::rotateAround(rad, pivotX, pivotY));
}

inline void mitiru::Screen::popTransform()
{
	if (!m_transformStack.empty())
	{
		m_transformStack.pop();
	}
}
